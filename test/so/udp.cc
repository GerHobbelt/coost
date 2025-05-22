#include "co/co.h"
#include "co/sock.h"
#include "co/flag.h"
#include "co/cout.h"

DEF_string(ip, "127.0.0.1", "ip");
DEF_int32(port, 6688, "port");

void udp_server_fun() {
    sock_t fd = co::udp_socket();
    co::sockaddr addr(FLG_ip.c_str(), (uint16)FLG_port);
    co::bind(fd, addr);

    co::sockaddr cli;
    char buf[4];
    char pong[4]; memcpy(pong, "pong", 4);

    co::print("server start: ", FLG_ip, ':', FLG_port);
    while (true) {
        cli.len = sizeof(cli.p);
        int r = co::recvfrom(fd, buf, 4, cli);
        if (r >= 0) {
            co::print("server recv ", fastring(buf, r), " from ", cli.str());

            r = co::sendto(fd, pong, 4, cli);
            if (r >= 0) {
                co::print("server send pong");
            } else {
                co::print("server sendto error: ", co::strerror());
                break;
            }
        } else {
            co::print("server recvfrom error: ", co::strerror());
            break;
        }
    }

    co::close(fd);
}

void udp_client_fun() {
    sock_t fd = co::udp_socket();
    co::sockaddr addr(FLG_ip.c_str(), (uint16)FLG_port);

    co::sockaddr peer;

    char buf[4];
    char ping[4]; memcpy(ping, "ping", 4);

    while (true) {
        int r = co::sendto(fd, ping, 4, addr);
        if (r == -1) {
            co::print("client sendto error: ", co::strerror());
            break;
        } else {
            co::print("client send ping");
            r = co::recvfrom(fd, buf, 4, peer);
            if (r == -1) {
                co::print("client recvform error: ", co::strerror());
                break;
            } else {
                co::print("client recv ", fastring(buf, r), " from ", peer.str(), '\n');
                co::sleep(3000);
            }
        }
    }

    co::close(fd);
}

int main(int argc, char** argv) {
    flag::parse(argc, argv);

    go(udp_server_fun);
    co::sleep(32);
    go(udp_client_fun);

    while (true) co::sleep(100000);

    return 0;
}
