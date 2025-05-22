#include "co/log.h"
#include "co/time.h"
#include "co/co.h"
#include "co/thread.h"

DEF_bool(t, false, "if true, run test in thread");
DEF_bool(m, false, "if true, run test in main thread");
DEF_bool(check, false, "if true, run CHECK test");

void a() {
    char* p = 0;
    if (FLG_check) {
        log::check_eq(1 + 1, 3);
    } else {
        *p = 'c';
    }
}

void b() {
    a();
}

void c() {
    b();
}

int main(int argc, char** argv) {
    flag::parse(argc, argv, true);

    if (FLG_m) {
        c();
    } else if (FLG_t) {
        co::thread(c).detach();
    } else {
        go(c);
    }

    while (true) time::sleep(7000);

    return 0;
}
