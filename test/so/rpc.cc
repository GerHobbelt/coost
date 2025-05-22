#include "rpc/hello_world.h"
#include "rpc/hello_again.h"
#include "co/co.h"
#include "co/flag.h"
#include "co/time.h"

DEF_bool(c, false, "client or server");
DEF_int32(n, 1, "req num");
DEF_int32(conn, 1, "conn num");
DEF_string(serv_ip, "127.0.0.1", "server ip");
DEF_int32(serv_port, 7788, "server port");
DEF_bool(ping, false, "test rpc ping");

namespace xx {

struct HelloWorldImpl : HelloWorld {
    HelloWorldImpl() = default;
    virtual ~HelloWorldImpl() = default;

    virtual void hello(co::Json& req, co::Json& res) {
        res = {
            { "data", {
                { "hello", 23 }
            }}
        };
    }

    virtual void world(co::Json& req, co::Json& res) {
        res = {
            { "error", "not supported"}
        };
    }
};

struct HelloAgainImpl : HelloAgain {
    HelloAgainImpl() = default;
    virtual ~HelloAgainImpl() = default;

    virtual void hello(co::Json& req, co::Json& res) {
        res = {
            { "data", {
                { "hello", "again" }
            }}
        };
    }

    virtual void again(co::Json& req, co::Json& res) {
        res = {
            { "error", "not supported"}
        };
    }
};

} // xx

// proto client
std::unique_ptr<rpc::client> proto;

// perform RPC request with rpc::Client
void test_rpc_client() {
    // copy a client from proto, 
    rpc::client c(*proto);

    for (int i = 0; i < FLG_n; ++i) {
        json::Json req, res;
        req.add_member("api", "HelloWorld.hello");
        c.call(req, res);
    }

    for (int i = 0; i < FLG_n; ++i) {
        co::Json req, res;
        req.add_member("api", "HelloAgain.again");
    }

    c.close();
}

co::pool pool(
    []() { return (void*) new rpc::client(*proto); },
    [](void* p) { delete (rpc::client*) p; }
);

void test_ping() {
    co::pooled_ptr<rpc::client> c(pool);
    while (true) {
        c->ping();
        co::sleep(3000);
    }
}

int main(int argc, char** argv) {
    flag::parse(argc, argv);
    flag::set_value("also_log2console", "true");

    // initialize the proto client, other client can simply copy from it.
    proto.reset(new rpc::client(FLG_serv_ip.c_str(), FLG_serv_port));

    if (!FLG_c) {
        rpc::server()
            .add_service(new xx::HelloWorldImpl)
            .add_service(new xx::HelloAgainImpl)
            .start("0.0.0.0", FLG_serv_port);
    } else {
        if (FLG_ping) {
            go(test_ping);
            go(test_ping);
        } else {
            for (int i = 0; i < FLG_conn; ++i) {
                go(test_rpc_client);
            }
        }
    }

    while (true) time::sleep(80000);
    return 0;
}
