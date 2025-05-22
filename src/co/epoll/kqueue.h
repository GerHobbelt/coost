#pragma once

#if !defined(_WIN32) && !defined(__linux__)
#include "co/sock.h"

namespace co {

struct CACHE_LINE_ALIGNED Kqueue {
    Kqueue();
    ~Kqueue();

    int wait(int ms);
    void signal();
    void handle_events();

    bool add_ev_read(sock_t fd, void* c);
    bool add_ev_write(sock_t fd, void* c);
    void del_ev_read(sock_t fd);
    void del_ev_write(sock_t fd);
    void del_event(sock_t fd);

    void _handle_ev_pipe();
  
    union {
        char _buf[L1_CACHE_LINE_SIZE];
        uint8 _signaled;
    };
    int _kq;
    int _pipe_fds[2];
    int _n;
    void* _events;
};

typedef Kqueue Epoll;

} // co

#endif
