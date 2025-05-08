#include "co/error.h"
#include "co/cout.h"

int main(int argc, char** argv) {
    for (int i = -3; i < 140; ++i) {
        co::cout("error: ", i, "  str: ", co::strerror(i), '\n');
    }

    for (int i = 10060; i < 10063; ++i) {
        co::cout("error: ", i, "  str: ", co::strerror(i), '\n');
    }

    return 0;
}
