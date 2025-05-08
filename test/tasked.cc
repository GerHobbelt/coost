#include "co/tasked.h"
#include "co/cout.h"
#include "co/time.h"

void f() {
    co::print("f(): ", time::str());
}

void g() {
    co::print("g(): ", time::str());
}

int main(int argc, char** argv) {
    co::print("now: ", time::str());

    co::tasked s;
    s.run_in(f, 0);
    s.run_in(f, 1);
    s.run_in(f, 2);
    s.run_in(f, 0);
    s.run_every(g, 3);
    s.run_at(f, 17, 12, 59);
    s.run_daily(f, 5, 18, 0);

    time::sleep(7000);
    s.stop();

    return 0;
}
