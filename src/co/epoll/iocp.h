#ifdef _WIN32
#pragma once

#include "co/sock.h"
#include "co/mem.h"
#include <functional>

#ifdef _MSC_VER
#pragma warning (disable:4200)
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <WinSock2.h>

namespace co {

struct alignas(co::cache_line_size) Iocp {
    typedef std::function<void(void*)> ev_cb_t;

    Iocp(ev_cb_t&& cb);
    ~Iocp();

    int wait(int ms);
    void signal();
    void handle_events();

    bool add_event(sock_t fd);
    bool add_ev_read(sock_t fd, void*) { return this->add_event(fd); }
    bool add_ev_write(sock_t fd, void*) { return this->add_event(fd); }

    void del_event(sock_t fd) {}
    void del_ev_read(sock_t fd) {}
    void del_ev_write(sock_t fd) {}

    union {
        char _buf[co::cache_line_size];
        uint8 _signaled;
    };
    int _n;
    HANDLE _iocp;
    OVERLAPPED_ENTRY* _events;
    ev_cb_t _cb;
};

typedef Iocp Epoll;

enum {
    io_tcp_nb, // for TCP non-blocking recv/send
    io_conn,   // for connect, accept
    io_udp,    // for UDP WSARecvFrom/WSASendTo
};

struct per_io_t {
    WSAOVERLAPPED ol;
    void* co;
    bool timeout;
    DWORD n;     // bytes transfered
    DWORD mlen;  // memory length
    DWORD flags; // flags for WSARecv, WSARecvFrom
    WSABUF buf;  // buffer for WSARecv, WSARecvFrom, WSASend, WSASendTo
    char s[];    // extra buffer allocated

    static per_io_t* create(void* co, int extra=0, int buf_size=0) {
        const uint32 m = sizeof(per_io_t) + extra;
        const uint32 n = m + buf_size;
        per_io_t* p = (per_io_t*) co::alloc(n);
        memset(p, 0, m);
        p->co = co;
        p->mlen = n;
        return p;
    }
};

} // co

#endif
