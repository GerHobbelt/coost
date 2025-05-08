#ifdef _WIN32

#include "iocp.h"
#include "co/error.h"
#include "../../dlog.h"

namespace co {

Iocp::Iocp(ev_cb_t&& cb)
    : _signaled(0), _n(0), _cb(std::move(cb))  {
    _iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 1);
    _events = (OVERLAPPED_ENTRY*) ::calloc(1024, sizeof(OVERLAPPED_ENTRY));
}

Iocp::~Iocp() {
    if (_iocp) { CloseHandle(_iocp); _iocp = 0; }
    if (_events) { ::free(_events); _events = 0; }
}

int Iocp::wait(int ms) {
    ULONG n = 0;
    if (GetQueuedCompletionStatusEx(
        _iocp, _events, 1024, &n, ms, false
    )) {
        _n = n;
    } else {
        const uint32 e = ::GetLastError();
        if (e == WAIT_TIMEOUT) {
            _n = 0;
        } else {
            _n = -1;
            CO_DLOG("IOCP wait error: ", co::strerror(e), ", handle: ", (void*)_iocp);
        }
    }
    return _n;
}

void Iocp::signal() {
    if (co::atomic_bool_cas(&_signaled, 0, 1, co::mo_acq_rel, co::mo_acquire)) {
        if (!PostQueuedCompletionStatus(_iocp, 0, 0, 0)) {
            CO_DLOG("IOCP signal error: ", co::strerror(), ", handle: ", _iocp);
        }
    }
}

bool Iocp::add_event(sock_t fd) {
    const auto h = CreateIoCompletionPort((HANDLE)fd, (HANDLE)_iocp, fd, 0);
    if (h != NULL) return true;

    const uint32 e = ::GetLastError();
    if (e == ERROR_ALREADY_EXISTS) return true;

    CO_DLOG("iocp add event error: ", co::strerror(e), ", fd: ", fd);
    return false;
}

void Iocp::handle_events() {
    for (int i = 0; i < _n; ++i) {
        auto& ev = _events[i];
        per_io_t* p = (per_io_t*) ev.lpOverlapped;
        if (p) {
            if (!p->timeout) {
                p->n = ev.dwNumberOfBytesTransferred;
                _cb(p->co);
            } else {
                CO_DLOG("free timedout per_io_t: ", (void*)p);
                co::free(p, p->mlen);
            }
        } else {
            atomic_store(&_signaled, 0, mo_release);
        }
    }
    _n = 0;
}

} // co

#endif
