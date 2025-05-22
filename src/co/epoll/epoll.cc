#ifdef __linux__
#include "epoll.h"
#include "co/error.h"
#include "co/log.h"
#include "../sched.h"
#include "../../close.h"

#include <unistd.h>
#include <sys/epoll.h>

namespace co {

Epoll::Epoll() : _signaled(0), _n(0) {
    _ep = ::epoll_create(1024);
    log::check_ne(_ep, -1);
    co::set_cloexec(_ep);

    int r = ::pipe(_pipe_fds);
    log::check_ne(r, -1);
    co::set_cloexec(_pipe_fds[0]);
    co::set_cloexec(_pipe_fds[1]);
    co::set_nonblock(_pipe_fds[0]);
    this->add_ev_read(_pipe_fds[0], nullptr);

    _events = (epoll_event*) ::calloc(1024, sizeof(epoll_event));
}

Epoll::~Epoll() {
    _close(_ep);
    _close(_pipe_fds[0]);
    _close(_pipe_fds[1]);
    if (_events) { ::free(_events); _events = 0; }
}

int Epoll::wait(int ms) {
    _n = ::epoll_wait(_ep, (epoll_event*)_events, 1024, ms);
    if (_n < 0) {
        log::error("epoll wait error: ", co::strerror(), ", fd: ", _ep);
    }
    return _n;
}

void Epoll::signal() {
    if (!_signaled && atomic_bool_cas(&_signaled, 0, 1, mo_acq_rel, mo_acquire)) {
        const char c = '0';
        const auto r = ::write(_pipe_fds[1], &c, 1);
        if (r < 0) {
            log::error("pipe write error: ", co::strerror(), ", fd: ", _pipe_fds[1]);
        }
    }
}

bool Epoll::add_ev_read(sock_t fd, void* c) {
    epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = c;

    const int r = ::epoll_ctl(_ep, EPOLL_CTL_ADD, fd, &ev);
    if (r == 0) return true;

    log::check_ne(
        errno, EEXIST, "did you read or write on the socket ", fd,
        " in different coroutines simultaneously?"
    );
    log::error("epoll add ev_read error: ", co::strerror(), ", fd: ", fd, " co: ", c);
    return false;
}

bool Epoll::add_ev_write(sock_t fd, void* c) {
    epoll_event ev;
    ev.events = EPOLLOUT | EPOLLET;
    ev.data.ptr = c;

    const int r = ::epoll_ctl(_ep, EPOLL_CTL_ADD, fd, &ev);
    if (r == 0) return true;

    log::check_ne(
        errno, EEXIST, "did you read or write on the socket ", fd,
        " in different coroutines simultaneously?"
    );
    log::error("epoll add ev_write error: ", co::strerror(), ", fd: ", fd, " co: ", c);
    return false;
}

void Epoll::del_event(sock_t fd) {
    const int r = ::epoll_ctl(_ep, EPOLL_CTL_DEL, fd, (epoll_event*)128);
    if (r != 0 && errno != ENOENT) {
        log::error("epoll del event error: ", co::strerror(), ", fd: ", fd);
    }
}

void Epoll::_handle_ev_pipe() {
    int32 dummy;
    while (true) {
        int r = ::read(_pipe_fds[0], &dummy, 4);
        if (r != -1) {
            if (r < 4) break;
            continue;
        } else {
            if (errno == EWOULDBLOCK || errno == EAGAIN) break;
            if (errno == EINTR) continue;
            log::error("pipe read error: ", co::strerror(), " fd: ", _pipe_fds[0]);
            break;
        }
    }
    atomic_store(&_signaled, 0, mo_release);
}

void Epoll::handle_events() {
    auto events = (epoll_event*)_events;
    for (int i = 0; i < _n; ++i) {
        const auto co = (Coroutine*) events[i].data.ptr;
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
