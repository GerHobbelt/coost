#include "co/tcp.h"
#include "co/co.h"
#include "co/error.h"
#include "co/log.h"
#include "co/atomic.h"
#include "co/mem.h"
#include "co/sock.h"

namespace tcp {

struct server_impl {
    server_impl() {
        _x.started = 0;
        _x.count = 0;
    }

    ~server_impl() = default;

    bool start(const char* ip, uint16 port);
    void loop();

    uint32 ref() {
        return co::atomic_inc(&_x.count, co::mo_relaxed);
    }

    void unref() {
        if (co::atomic_dec(&_x.count, co::mo_acq_rel) == 0) {
            this->~server_impl();
            co::free(this, sizeof(server_impl));
        }
    }

    void on_connection(sock_t fd);

    union {
        struct {
            int32 started;
            uint32 count;
        } _x;
        uint32 _[co::cache_line_size / sizeof(uint32)];
    };

    const char* _ip;
    uint16 _port;
    sock_t _fd;
    sock_t _connfd;
    co::sockaddr _addr;
    conn_cb_t _conn_cb;
};

void server_impl::on_connection(sock_t fd) {
    co::set_tcp_keepalive(fd);
    co::set_tcp_nodelay(fd);
    _conn_cb(fd);
    this->unref();
}

bool server_impl::start(const char* ip, uint16 port) {
    if (!_conn_cb) {
        log::info("server start failed: connection callback not set");
        return false;
    }

    _ip = (ip && *ip) ? ip : "0.0.0.0";
    _port = port;
    go(&server_impl::loop, this);

    int s;
    while (true) {
        s = co::atomic_load(&_x.started, co::mo_relaxed);
        if (s != 0) break;
        co::sleep(1);
    }
    return s > 0;
}

void server_impl::loop() {
    _fd = co::tcp_server_socket(_ip, _port, 64 * 1024);
    if (_fd == (sock_t)-1) {
        co::atomic_store(&_x.started, -1, co::mo_relaxed);
        return;
    }

    this->ref();
    co::atomic_store(&_x.started, 1, co::mo_relaxed);
    log::info("server start: ", _ip, ':', _port);

    while (true) {
        _addr.len = sizeof(_addr.p);
        _connfd = co::accept(_fd, &_addr);

        if (_connfd != (sock_t)-1) {
            const uint32 n = this->ref() - 1;
            log::info(
                "server(", _ip, ':', _port, ") accept connection: ", _addr.str(),
                ", connfd: ", _connfd, ", conn num: ", n
            );
            go(&server_impl::on_connection, this, _connfd);
        } else {
            log::warn(
                "server(", _ip, ':', _port, ") accept error: ", co::strerror()
            );
        }
    }
}

server::server() {
    _p = co::alloc(sizeof(server_impl), co::cache_line_size);
    new (_p) server_impl();
}

server::~server() {
    if (_p) {
        auto p = (server_impl*)_p;
        if (!p->_x.started) co::_delete(p);
        _p = nullptr;
    }
}

server& server::on_connection(conn_cb_t&& cb) {
    ((server_impl*)_p)->_conn_cb = std::move(cb);
    return *this;
}

bool server::start(const char* ip, int port) {
    return ((server_impl*)_p)->start(ip, port);
}

client::client(const char* host, uint16 port) {
    _server_host = (host && *host) ? host : "127.0.0.1";
    _server_port = port;
    _fd = (sock_t)-1;
}

client::client(const client& c) {
    _server_host = c._server_host;
    _server_port = c._server_port;
    _fd = (sock_t)-1;
}

client::~client() {
    this->disconnect();
}

bool client::connect(int ms) {
    if (this->connected()) return true;

    int r;
    co::sockaddr addr(_server_host, _server_port);
    if (!addr.valid) {
        log::error(
            "connect failed, invalid server addr: ", _server_host, ':', _server_port
        );
        goto end;
    }

    _fd = co::tcp_socket(addr.af());
    if (_fd == (sock_t)-1) goto end;

    r = co::connect(_fd, addr, ms);
    if (r != 0) {
        log::error(
            "connect to ", _server_host, ':', _server_port, " failed: ", co::strerror()
        );
        goto end;
    }

    co::set_tcp_nodelay(_fd);
    return true;

end:
    this->disconnect();
    return false;
}

void client::disconnect() {
    if (this->connected()) {
        co::close(_fd);
        _fd = (sock_t)-1;
    }
}

} // tcp
