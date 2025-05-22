#ifdef _WIN32
#pragma once

#include "co/sock.h"

#ifdef _MSC_VER
#pragma warning (disable:4200)
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <WinSock2.h>

namespace co {

struct CACHE_LINE_ALIGNED Iocp {
    Iocp();
    ~Iocp();

    int wait(int ms);
    void signal();
    void handle_events();

    bool add_event(sock_t fd);
    bool add_ev_read(sock_t fd, void*)  { return this->add_event(fd); }
    bool add_ev_write(sock_t fd, void*) { return this->add_event(fd); }

    void del_event(sock_t fd) {}
    void del_ev_read(sock_t fd) {}
    void del_ev_write(sock_t fd) {}

    union {
        char _buf[L1_CACHE_LINE_SIZE];
        uint8 _signaled;
    };
    int _n;
    HANDLE _iocp;
    OVERLAPPED_ENTRY* _events;
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
    uint8 state;
    DWORD n;     // bytes transfered
    DWORD mlen;  // memory length
    DWORD flags; // flags for WSARecv, WSARecvFrom
    WSABUF buf;  // buffer for WSARecv, WSARecvFrom, WSASend, WSASendTo
    char s[];    // extra buffer allocated

    static per_io_t* create(void* co, int extra=0, int buf_size=0);
};

} // co

#endif
