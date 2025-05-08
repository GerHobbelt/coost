#include "co/co.h"
#include "co/cout.h"
#include "co/flag.h"

static int g_id;

struct S {
    S() { _v = this->get_id(); }
    ~S() = default;

    void run() {
        co::print("S: ", _v);
    }

    int get_id() {
        return co::atomic_inc(&g_id);
    }

    int _v;
};

// use DEF_main to make code in main() also run in coroutine.
int main(int argc, char** argv) {
    flag::parse(argc, argv);
    
    co::pool p(
        []() { return (void*) co::_new<S>(); },
        [](void* p) { co::_delete((S*)p); },
        1024
    );

    co::wait_group wg;

    do {
        co::print("test pop/push begin: ");
        wg.add(8);
        for (int i = 0; i < 8; ++i) {
            co::print("go: ", i);
            go([p, wg]() { /* capture p and wg by value here, as they are on stack */
                S* s = (S*)p.pop();
                s->run();
                p.push(s);
                wg.done();
            });
        }
        wg.wait();
        co::print("test pop/push end.. \n");
    } while (0);

    do {
        co::print("test co::pooled_ptr begin: ");
        wg.add(8);
        for (int i = 0; i < 8; ++i) {
            go([p, wg]() { /* capture p and wg by value here, as they are on stack */
                {
                    co::pooled_ptr<S> s(p);
                    s->run();
                }
                wg.done();
            });
        }
        wg.wait();
        co::print("test co::pooled_ptr end..\n");
    } while (0);

    return 0;
}
