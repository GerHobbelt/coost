#pragma once

#include "sock.h"
#include <functional>

namespace tcp {

typedef std::function<void(sock_t)> conn_cb_t;

struct server {
    server();
    ~server();

    // set connection callback
    server& on_connection(conn_cb_t&& cb);

    server& on_connection(const conn_cb_t& cb) {
        return this->on_connection(conn_cb_t(cb));
    }

    bool start(const char* ip, int port);

    void* _p;
    DISALLOW_COPY_AND_ASSIGN(server);
};

struct client {
    client(const char* server_host, uint16 server_port);
    client(const client& c);
    ~client();

    void operator=(const client& c) = delete;

    bool connected() const { return _fd != (sock_t)-1; }

    bool connect(int ms);
    void disconnect();
    void close() { this->disconnect(); }

    int recv(void* buf, int n, int ms=-1) {
        return co::recv(_fd, buf, n, ms);
    }

    int recvn(void* buf, int n, int ms=-1) {
        return co::recvn(_fd, buf, n, ms);
    }

    int send(const void* buf, int n, int ms=-1) {
        return co::send(_fd, buf, n, ms);
    }

    const char* _server_host;
    uint16 _server_port;
    sock_t _fd;
};

} // tcp
