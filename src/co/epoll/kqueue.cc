#if !defined(_WIN32) && !defined(__linux__)
#include "kqueue.h"
#include "co/error.h"
#include "co/log.h"
#include "../sched.h"
#include "../../close.h"

#include <time.h>
#include <unistd.h>
#include <sys/event.h>

namespace co {

Kqueue::Kqueue() : _signaled(0), _n(0) {
    _kq = kqueue();
    log::check_ne(_kq, -1);

    int r = ::pipe(_pipe_fds);
    log::check_ne(r, -1);
    co::set_cloexec(_pipe_fds[0]);
    co::set_cloexec(_pipe_fds[1]);
    co::set_nonblock(_pipe_fds[0]);
    this->add_ev_read(_pipe_fds[0], nullptr);

    _events = (struct kevent*) ::calloc(1024, sizeof(struct kevent));
}

Kqueue::~Kqueue() {
    _close(_kq);
    _close(_pipe_fds[0]);
    _close(_pipe_fds[1]);
    if (_events) { ::free(_events); _events = 0; }
}

int Kqueue::wait(int ms) {
    if (ms >= 0) {
        struct timespec ts = { ms / 1000, ms % 1000 * 1000000 };
        _n = ::kevent(_kq, 0, 0, (struct kevent*)_events, 1024, &ts);
    } else {
        _n = ::kevent(_kq, 0, 0, (struct kevent*)_events, 1024, 0);
    }
    if (_n < 0) {
        log::error("kqueue wait error: ", co::strerror(), ", fd: ", _kq);
    }
    return _n;
}

void Kqueue::signal() {
    if (!_signaled && atomic_bool_cas(&_signaled, 0, 1, mo_acq_rel, mo_acquire)) {
        const char c = '0';
        const auto r = ::write(_pipe_fds[1], &c, 1);
        if (r < 0) {
            log::error("pipe write error: ", co::strerror(), ", fd: ", _pipe_fds[1]);
        }
    }
}

bool Kqueue::add_ev_read(int fd, void* c) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, c);
    const int r = ::kevent(_kq, &ev, 1, 0, 0, 0);
    if (r >= 0) return true;

    log::error("kqueue add ev_read error: ", co::strerror(), ", fd: ", fd, " co: ", c);
    return false;
}

bool Kqueue::add_ev_write(int fd, void* c) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD, 0, 0, c);
    const int r = ::kevent(_kq, &ev, 1, 0, 0, 0);
    if (r >= 0) return true;

    log::error("kqueue add ev_write error: ", co::strerror(), ", fd: ", fd, " co: ", c);
    return false;
}

void Kqueue::del_ev_read(int fd) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
    if (::kevent(_kq, &ev, 1, 0, 0, 0) < 0 && errno != ENOENT) {
        log::error("kqueue del ev_read error: ", co::strerror(), ", fd: ", fd);
    }
}

void Kqueue::del_ev_write(int fd) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
    if (::kevent(_kq, &ev, 1, 0, 0, 0) < 0 && errno != ENOENT) {
        log::error("kqueue del ev_write error: ", co::strerror(), ", fd: ", fd);
    }
}

void Kqueue::del_event(int fd) {
    struct kevent ev[2];
    EV_SET(&ev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
    EV_SET(&ev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
    if (::kevent(_kq, ev, 2, 0, 0, 0) < 0 && errno != ENOENT) {
        log::error("kqueue del event error: ", co::strerror(), ", fd: ", fd);
    }
}

void Kqueue::_handle_ev_pipe() {
    int32 dummy;
    while (true) {
        int r = ::read(_pipe_fds[0], &dummy, 4);
        if (r != -1) {
            if (r < 4) break;
            continue;
        } else {
            if (errno == EWOULDBLOCK || errno == EAGAIN) break;
            if (errno == EINTR) continue;
            log::error("pipe read error: ", co::strerror(), ", fd: ", _pipe_fds[0]);
            break;
        }
    }
    atomic_store(&_signaled, 0, mo_release);
}

void Kqueue::handle_events() {
    auto events = (struct kevent*)_events;
    for (int i = 0; i < _n; ++i) {
        const auto co = (Coroutine*) events[i].udata;
        if (co) {
            co->sched->resume(co);
        } else {
            this->_handle_ev_pipe();
        }
    }
    _n = 0;
}

} // co

#endif
