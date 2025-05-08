#include "co/all.h"

DEF_int32(n, 15, "id length");
DEF_string(s, "0-9a-zA-Z", "symbols");

int main(int argc, char** argv) {
    flag::parse(argc, argv);
    if (FLG_s.empty()) {
        co::print(co::randstr(FLG_n));
    } else {
        co::print(co::randstr(FLG_s.c_str(), FLG_n));
    }
    return 0;
}
