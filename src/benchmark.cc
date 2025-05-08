#include "co/benchmark.h"
#include "co/fastring.h"
#include "co/cout.h"

namespace co {
namespace bm {

static co::vector<Group>* g_g;

inline co::vector<Group>& groups() {
    return g_g ? *g_g : *(g_g = []() {
        auto g = co::_make_static<co::vector<Group>>();
        g->reserve(8);
        return g;
    }());
}

bool add_group(const char* name, void (*f)(Group&)) {
    groups().emplace_back(name, f);
    return false;
}

// do nothing, just fool the compiler
void use(void*, int) {}

struct Num {
    constexpr Num(double v) noexcept : v(v) {}

    fastring str() const {
        fastring s(16);
        if (v < 0.01) {
            s = "< 0.01";
        } else if (v < 1000.0) {
            s << co::dp::_2(v);
        } else if (v < 1000000.0) {
            s << co::dp::_2(v / 1000) << 'K';
        } else if (v < 1000000000.0) {
            s << co::dp::_2(v / 1000000) << 'M';
        } else {
            const double x = v / 1000000000;
            if (x <= 1000.0) {
                s << co::dp::_2(x) << 'G';
            } else {
                s << "> 1000G";
            }
        }
        return s;
    }

    double v;
};

// |  group  |  ns/iter  |  iters/s  |  speedup  |
// | ------- | --------- | --------- | --------- |
// |  bm 0   |  50.0     |  20.0M    |  1.0      |
// |  bm 1   |  10.0     |  100.0M   |  5.0      |
void print_results(Group& g) {
    size_t grplen = ::strlen(g.name);
    size_t maxlen = grplen;
    for (auto& r : g.res) {
        const size_t x = ::strlen(r.bm);
        if (maxlen < x) maxlen = x;
    }

    const fastring _9_(9, '-');
    co::cout(
        "|  ", co::text(g.name, color::bright_magenta), fastring(maxlen - grplen + 2, ' '),
        "|  ", co::text("ns/iter  ", color::bright_blue),
        "|  ", co::text("iters/s  ", color::bright_blue),
        "|  ", co::text("speedup  ", color::bright_blue),
        '|', '\n',
        "| ", fastring(maxlen + 2, '-'), ' ',
        "| ", _9_, ' ',
        "| ", _9_, ' ',
        "| ", _9_, ' ',
        '|', '\n'
    );

    for (size_t i = 0; i < g.res.size(); ++i) {
        auto& r = g.res[i];
        const size_t bmlen = ::strlen(r.bm);
        fastring t = Num(r.ns).str();
        size_t p = t.size() <= 7 ? 9 - t.size() : 2;

        co::cout(
            "|  ", co::text(r.bm, color::bright_green), fastring(maxlen - bmlen + 2, ' '),
            "|  ", co::text(t.c_str(), color::bright_cyan), fastring(p, ' ')
        );

        double x = r.ns > 0 ? 1000000000.0 / r.ns : 1.2e12;
        t = Num(x).str();
        p = t.size() <= 7 ? 9 - t.size() : 2;
        co::cout("|  ", co::text(t.c_str(), color::bright_cyan), fastring(p, ' '));

        if (i == 0) {
            t = "1.0";
        } else {
            auto _ = g.res[0].ns;
            x = r.ns > 0 ? _ / r.ns : (_ > 0 ? 1.2e12 : 1.0);
            t = Num(x).str();
        }

        p = t.size() <= 7 ? 9 - t.size() : 2;
        co::cout("|  ", co::text(t.c_str(), color::bright_yellow), fastring(p, ' '), '|', '\n');
    }
}

} // bm

void run_benchmarks() {
    auto& groups = bm::groups();
    for (size_t i = 0; i < groups.size(); ++i) {
        if (i != 0) co::cout('\n');
        auto& g = groups[i];
        g.f(g);
        bm::print_results(g);
    }
}

} // co
