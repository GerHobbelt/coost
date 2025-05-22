#include "co/rpc.h"
#include "co/def.h"
#include "co/sock.h"
#include "co/tcp.h"
#include "co/co.h"
#include "co/flag.h"
#include "co/log.h"
#include "co/fastring.h"

DEF_mls(rpc_max_msg_size, "@i RPC 最大消息长度", "@i max size of RPC message");
DEF_mls(rpc_recv_timeout, "@i RPC 接收超时时间(毫秒)", "@i RPC recv timeout(ms)");
DEF_mls(rpc_send_timeout, "@i RPC 发送超时时间(毫秒)", "@i RPC send timeout(ms)");
DEF_mls(rpc_conn_timeout, "@i RPC 连接超时时间(毫秒)", "@i RPC connect timeout(ms)");
DEF_mls(rpc_conn_idle_sec, "@i RPC 连接最大空闲时间(秒)", "@i RPC max connection idle time(seconds)");
DEF_mls(rpc_max_idle_conn, "@i RPC 最大空闲连接数", "@i max idle RPC connection");
DEF_mls(rpc_log, "@i 打印RPC日志", "@i print RPC log");

DEF_int32(rpc_max_msg_size, 8 << 20, MLS_rpc_max_msg_size);
DEF_int32(rpc_recv_timeout, 3000, MLS_rpc_recv_timeout);
DEF_int32(rpc_send_timeout, 3000, MLS_rpc_send_timeout);
DEF_int32(rpc_conn_timeout, 3000, MLS_rpc_conn_timeout);
DEF_int32(rpc_conn_idle_sec, 180, MLS_rpc_conn_idle_sec);
DEF_int32(rpc_max_idle_conn, 128, MLS_rpc_max_idle_conn);
DEF_bool(rpc_log, true, MLS_rpc_log);

#define RPCLOG if (FLG_rpc_log) log::info

namespace rpc {

struct Header {
    uint16 flags; // reserved, 0
    uint16 magic; // 0x7777
    uint32 len;   // body len
}; // 8 bytes

static const uint16 g_magic = 0x7777;

inline void set_header(const void* header, uint32 msg_len) {
    ((Header*)header)->flags = 0;
    ((Header*)header)->magic = g_magic;
    ((Header*)header)->len = co::hton32(msg_len);
}

struct server_impl {
    static void ping(json::Json&, json::Json& res) {
        res.add_member("res", "pong");
    }

    server_impl() : _started(false) {
        _methods["ping"] = &server_impl::ping;
    }

    ~server_impl() = default;

    void add_service(const std::shared_ptr<service>& s);

    method_t* find_method(const char* name);

    void on_connection(sock_t fd);

    bool start(const char* ip, int port);

    void process(json::Json& req, json::Json& res);

    tcp::server _tcp_serv;
    bool _started;
    co::hash_map<const char*, std::shared_ptr<service>> _services;
    co::hash_map<const char*, method_t> _methods;
};

void server_impl::add_service(const std::shared_ptr<service>& s) {
    _services[s->name()] = s;
    for (auto& x : s->methods()) {
        _methods[x.first] = x.second;
    }
}

inline method_t* server_impl::find_method(const char* name) {
    auto it = _methods.find(name);
    return it != _methods.end() ? &it->second : nullptr;
}

bool server_impl::start(const char* ip, int port) {
    _tcp_serv.on_connection([this](sock_t fd) {
        this->on_connection(fd);
    });
    _started = _tcp_serv.start(ip, port);
    return _started;
}

void server_impl::process(json::Json& req, json::Json& res) {
    auto& x = req.get("api");
    if (x.is_string()) {
        auto m = this->find_method(x.as_c_str());
        if (m) {
            (*m)(req, res);
        } else {
            res.add_member("error", "api not found");
        }
    } else {
        res.add_member("error", "string filed 'api' not found in req");
    }
}

void server_impl::on_connection(sock_t fd) {
    int r = 0, len = 0;
    Header header;
    fastring buf;
    json::Json req, res;

_rpc:
    r = co::recvn(fd, &header, sizeof(header), FLG_rpc_conn_idle_sec * 1000);
    if_unlikely (r == 0) goto recv_zero_err;
    if_unlikely (r < 0) {
        if (!co::timeout()) goto recv_err;
        if (_tcp_serv.conn_num() > FLG_rpc_max_idle_conn) goto idle_err;
        buf.reset();
        goto _rpc;
    }

    if_unlikely (header.magic != g_magic) goto magic_err;

    len = co::ntoh32(header.len);
    if_unlikely (len > FLG_rpc_max_msg_size) goto msg_too_long_err;

    if (buf.capacity() == 0) buf.reserve(4096);
    buf.resize(len);
    r = co::recvn(fd, buf.data(), len, FLG_rpc_recv_timeout);
    if_unlikely (r == 0) goto recv_zero_err;
    if_unlikely (r < 0) goto recv_err;

    req = json::parse(buf.data(), buf.size());
    if_unlikely (req.is_null()) goto json_parse_err;

    RPCLOG("rpc recv req: ", req);
    res.reset();
    this->process(req, res);

    buf.resize(sizeof(Header));
    res.str(buf);
    set_header(buf.data(), (uint32)(buf.size() - sizeof(Header)));
    
    r = co::send(fd, buf.data(), (int)buf.size(), FLG_rpc_send_timeout);
    if_unlikely (r <= 0) goto send_err;
    RPCLOG("rpc send res: ", res);
    goto _rpc;

recv_zero_err:
    log::info("rpc client close the connection, connfd: ", fd);
    co::close(fd);
    goto end;
idle_err:
    log::info("rpc close idle connection, connfd: ", fd);
    co::reset_tcp_socket(fd);
    goto end;
magic_err:
    log::error("rpc recv error: bad magic number");
    goto reset_conn;
msg_too_long_err:
    log::error("rpc recv error: body too long");
    goto reset_conn;
recv_err:
    log::error("rpc recv error: ", co::strerror());
    goto reset_conn;
send_err:
    log::error("rpc send error: ", co::strerror());
    goto reset_conn;
json_parse_err:
    log::error("rpc json parse error: ", buf);
    goto reset_conn;
reset_conn:
    co::reset_tcp_socket(fd, 1000);
end:
    return;
}

server::server() {
    _p = co::_new<server_impl>();
}

server::~server() {
    if (_p) {
        auto p = (server_impl*)_p;
        if (!p->_started) co::_delete(p);
        _p = 0;
    }
}

server& server::add_service(const std::shared_ptr<service>& s) {
    ((server_impl*)_p)->add_service(s);
    return *this;
}

bool server::start(const char* ip, int port) {
    return ((server_impl*)_p)->start(ip, port);
}


struct client_impl {
    client_impl(const char* ip, int port)
        : _tcp_cli(ip, port) {
    }

    client_impl(const client_impl& c)
        : _tcp_cli(c._tcp_cli) {
    }

    ~client_impl() = default;

    void call(const json::Json& req, json::Json& res);

    tcp::client _tcp_cli;
    fastream _fs;
};

void client_impl::call(const json::Json& req, json::Json& res) {
    int r = 0, len = 0;
    Header header;
    if (!_tcp_cli.connected() && !_tcp_cli.connect(FLG_rpc_conn_timeout)) return;

    _fs.resize(sizeof(Header));
    req.str(_fs);
    set_header((void*)_fs.data(), (uint32)(_fs.size() - sizeof(Header)));

    r = _tcp_cli.send(_fs.data(), (int)_fs.size(), FLG_rpc_send_timeout);
    if_unlikely (r <= 0) goto send_err;
    RPCLOG("rpc send req: ", req);

    r = _tcp_cli.recvn(&header, sizeof(header), FLG_rpc_recv_timeout);
    if_unlikely(r == 0) goto recv_zero_err;
    if_unlikely(r < 0) goto recv_err;
    if_unlikely(header.magic != g_magic) goto magic_err;

    len = co::ntoh32(header.len);
    if_unlikely(len > FLG_rpc_max_msg_size) goto msg_too_long_err;

    _fs.resize(len);
    r = _tcp_cli.recvn(_fs.data(), len, FLG_rpc_recv_timeout);
    if_unlikely(r == 0) goto recv_zero_err;
    if_unlikely(r < 0) goto recv_err;

    res = json::parse(_fs.data(), _fs.size());
    if (res.is_null()) goto json_parse_err;
    RPCLOG("rpc recv res: ", res);
    return;

magic_err:
    log::error("rpc recv error: bad magic number: ", header.magic);
    goto err_end;
msg_too_long_err:
    log::error("rpc recv error: body too long");
    goto err_end;
recv_zero_err:
    log::error("rpc server close the connection..");
    goto err_end;
recv_err:
    log::error("rpc recv error: ", co::strerror());
    goto err_end;
send_err:
    log::error("rpc send error: ", co::strerror());
    goto err_end;
json_parse_err:
    log::error("rpc json parse error: ", _fs);
    goto err_end;
err_end:
    _tcp_cli.disconnect();
}

client::client(const char* ip, int port) {
    _p = co::_new<client_impl>(ip, port);
}

client::client(const client& c) {
    _p = co::_new<client_impl>(*(client_impl*)c._p);
}

client::~client() {
    co::_delete((client_impl*)_p);
}

void client::call(const json::Json& req, json::Json& res) {
    return ((client_impl*)_p)->call(req, res);
}

void client::close() {
    return ((client_impl*)_p)->_tcp_cli.close();
}

void client::ping() {
    json::Json req({{"api", "ping"}}), res;
    this->call(req, res);
}

namespace xx {

static void unhide_flags() {
    flag::set_attr("rpc_max_msg_size", flag::attr_default);
    flag::set_attr("rpc_recv_timeout", flag::attr_default);
    flag::set_attr("rpc_send_timeout", flag::attr_default);
    flag::set_attr("rpc_conn_timeout", flag::attr_default);
    flag::set_attr("rpc_conn_idle_sec", flag::attr_default);
    flag::set_attr("rpc_max_idle_conn", flag::attr_default);
    flag::set_attr("rpc_log", flag::attr_default);
}

static int g_nifty_counter;

RpcInit::RpcInit() {
    const int n = ++g_nifty_counter;
    if (n == 2) flag::run_before_parse(unhide_flags);
}

} // xx
} // rpc
