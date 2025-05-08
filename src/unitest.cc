#include "co/unitest.h"
#include "co/time.h"

namespace co {
namespace ut {

static co::vector<Test>* g_t;
inline co::vector<Test>& tests() {
    return g_t ? *g_t : *(g_t = co::_make_static<co::vector<Test>>());
}

bool add_test(const char* name, bool& e, void(*f)(Test&)) {
    tests().emplace_back(name, e, f);
    return false;
}

} // ut

int run_unitests() {
    // n: number of tests to do
    // ft: number of failed tests
    // fc: number of failed cases
    size_t n = 0, ft = 0, fc = 0;
    time::timer timer;
    auto& tests = ut::tests();

    co::vector<ut::Test*> enabled;
    for (auto& t: tests) if (t.enabled) enabled.push_back(&t);

    if (enabled.empty()) { /* run all tests by default */
        n = tests.size();
        for (auto& t : tests) {
            co::cout("> begin test: ", t.name, '\n');
            timer.restart();
            t.f(t);
            if (!t.failed.empty()) { ++ft; fc += t.failed.size(); }
            co::cout("< test ", t.name, " done in ", timer.us(), " us", co::endl);
        }

    } else {
        n = enabled.size();
        for (auto& t: enabled) {
            co::cout("> begin test: ", t->name, '\n');
            timer.restart();
            t->f(*t);
            if (!t->failed.empty()) { ++ft; fc += t->failed.size(); }
            co::cout("< test ", t->name, " done in ", timer.us(), " us", co::endl);
        }
    }

    if (fc == 0) {
        if (n > 0) {
            co::cout(color::green, "\nNice! All tests passed!", color::deflt, co::endl);
        } else {
            co::cout("No test found. Done nothing.", co::endl);
        }

    } else {
        co::cout(color::red,
            "\nAha! ", fc, " case", (fc > 1 ? "s" : ""),
            " from ", ft, " test", (ft > 1 ? "s" : ""),
            " failed:\n\n", color::deflt
        );

        const char* last_case = "";
        for (auto& t : tests) {
            if (!t.failed.empty()) {
                co::cout(color::red, "In test ", t.name, ":\n", color::deflt);
                for (auto& f : t.failed) {
                    if (strcmp(last_case, f.c) != 0) {
                        last_case = f.c;
                        co::cout(color::red, " case ", f.c, ":\n", color::deflt);
                    }
                    co::cout(color::yellow, "  ", f.file, ':', f.line, "] ", color::deflt, f.msg, '\n');
                }
                co::cout().flush();
            }
        }

        co::cout(color::deflt);
        co::cout().flush();
    }

    return (int)fc;
}

} // co
