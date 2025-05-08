#ifdef __linux__
#pragma once

#include "co/sock.h"
#include "co/mem.h"
#include <functional>

namespace co {

struct alignas(co::cache_line_size) Epoll {
    typedef std::function<void(void*)> ev_cb_t;

    Epoll(ev_cb_t&& cb);
    ~Epoll();

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
        char _buf[co::cache_line_size];
        uint8 _signaled;
    };
    int _ep;
    int _pipe_fds[2];
    int _n;
    void* _events;
    ev_cb_t _cb;
};

} // co

#endif
