#pragma once

#include "json.h"
#include "stl.h"
#include <memory>
#include <functional>

namespace rpc {
namespace xx {

struct RpcInit {
    RpcInit();
    ~RpcInit() = default;
};

static RpcInit g_rpc_init;

} // xx

typedef std::function<void(json::Json&, json::Json&)> method_t;

struct service {
    service() = default;
    virtual ~service() = default;

    virtual const char* name() const = 0;
    virtual const co::map<const char*, method_t>& methods() const = 0;
};

struct server {
    server();
    ~server();

    server& add_service(const std::shared_ptr<service>& s);

    server& add_service(service* s) {
        return this->add_service(std::shared_ptr<service>(s));
    }

    bool start(const char* ip, int port);

    void* _p;
    DISALLOW_COPY_AND_ASSIGN(server);
};

struct client {
    client(const char* ip, int port);
    client(const client& c);
    ~client();
    void operator=(const client& c) = delete;

    void call(const json::Json& req, json::Json& res);

    void ping();

    void close();

    void* _p;
};

} // rpc
