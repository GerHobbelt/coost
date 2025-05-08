#pragma once

#if !defined(_WIN32) && !defined(__linux__)
#include "co/sock.h"
#include "co/mem.h"
#include <functional>

namespace co {

struct alignas(co::cache_line_size) Kqueue {
    typedef std::function<void(void*)> ev_cb_t;

    Kqueue(ev_cb_t&& cb);
    ~Kqueue();

    int wait(int ms);
    void signal();
    void handle_events();

    bool add_ev_read(int fd, void* c);
    bool add_ev_write(int fd, void* c);
    void del_ev_read(int fd);
    void del_ev_write(int fd);
    void del_event(int fd);

    void _handle_ev_pipe();
  
    union {
        char _buf[co::cache_line_size];
        uint8 _signaled;
    };
    int _kq;
    int _pipe_fds[2];
    int _n;
    void* _events;
    ev_cb_t _cb;
};

typedef Kqueue Epoll;

} // co

#endif
