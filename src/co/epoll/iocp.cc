#ifdef _WIN32

#include "iocp.h"
#include "co/error.h"
#include "co/log.h"
#include "../sched.h"

namespace co {

Iocp::Iocp()
    : _signaled(0), _n(0) {
    _iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 1);
    _events = (OVERLAPPED_ENTRY*) ::calloc(1024, sizeof(OVERLAPPED_ENTRY));
}

Iocp::~Iocp() {
    if (_iocp) { CloseHandle(_iocp); _iocp = 0; }
    if (_events) { ::free(_events); _events = 0; }
}

int Iocp::wait(int ms) {
    ULONG n = 0;
    if (GetQueuedCompletionStatusEx(_iocp, _events, 1024, &n, ms, false)) {
        _n = (int)n;
    } else {
        const uint32 e = ::GetLastError();
        if (e == WAIT_TIMEOUT) {
            _n = 0;
        } else {
            _n = -1;
            log::error("IOCP wait error: ", co::strerror(e), ", handle: ", (void*)_iocp);
        }
    }
    return _n;
}

void Iocp::signal() {
    if (!_signaled && atomic_bool_cas(&_signaled, 0, 1, mo_acq_rel, mo_acquire)) {
        if (!PostQueuedCompletionStatus(_iocp, 0, 0, 0)) {
            log::error("IOCP signal error: ", co::strerror(), ", handle: ", _iocp);
        }
    }
}

bool Iocp::add_event(sock_t fd) {
    CreateIoCompletionPort((HANDLE)fd, _iocp, fd, 0);
    return true; // always return true
}

void Iocp::handle_events() {
    const auto s = g_sched;
    for (int i = 0; i < _n; ++i) {
        auto& ev = _events[i];
        per_io_t* p = (per_io_t*) ev.lpOverlapped;
        if (p) {
            Coroutine* co = (Coroutine*) p->co;
            p->n = ev.dwNumberOfBytesTransferred;
            if (atomic_bool_cas(&p->state, st_wait, st_ready, mo_release, mo_relaxed)) {
                if (co->sched == s) {
                    s->resume(co);
                } else {
                    co->sched->add_ready_task(co);
                }
            } else {
                co::free(p, p->mlen);
            }
        } else {
            atomic_store(&_signaled, 0, mo_release);
        }
    }
    _n = 0;
}

per_io_t* per_io_t::create(void* co, int extra, int buf_size) {
    const uint32 m = sizeof(per_io_t) + extra;
    const uint32 n = m + buf_size;
    per_io_t* p = (per_io_t*) co::alloc(n);
    memset(p, 0, m);
    p->co = co;
    p->state = st_wait;
    p->mlen = n;
    ((Coroutine*)co)->wtx = (Waitx*)(((char*)&p->co) - offsetof(Waitx, co));
    return p;
}

} // co

#endif
