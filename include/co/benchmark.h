#pragma once

#include "def.h"
#include "stl.h"
#include "time.h"

namespace co {

void run_benchmarks();

namespace bm {

struct Result {
    Result(const char* bm, double ns) noexcept : bm(bm), ns(ns) {}
    const char* bm;
    double ns;
};

struct Group {
    Group(const char* name, void (*f)(Group&)) noexcept : name(name), f(f) {}

    void start(const char* bm) {
        c = 1, iters = 0;
        res.emplace_back(bm, 0);
        timer.restart();
    }

    void end() {
        const int64 t = timer.ns();
        res.back().ns = (iters > 1 ? t : ns) * 1.0 / iters;
    }

    void _the_first_call() {
        ns = timer.ns();
        c = iters = [](int64 ns) {
            if (ns <= 1000) return 100 * 1000;
            if (ns <= 10000) return 10 * 1000;
            if (ns <= 100000) return 1000;
            if (ns <= 1000000) return 100;
            if (ns <= 10000000) return 10;
            return 1;
        }(ns);
        iters > 1 ? timer.restart() : (void)(c = 0);
    }

    bool done() const { return c == 0; }
    void goon() { iters != 0 ? (void)--c : _the_first_call(); }

    const char* name;
    void (*f)(Group&);
    int c;
    int iters;
    int64 ns;
    time::timer timer;
    co::vector<Result> res;
};

struct Runner {
    Runner(Group& g, const char* bm) : g(g) { g.start(bm); }
    ~Runner() { g.end(); }
    Group& g;
};

bool add_group(const char* name, void (*f)(Group&));
void use(void* p, int n);

} // bm
} // co

// define a benchmark group
#define BM_group(name) \
    void _co_bm_##name(co::bm::Group&); \
    static bool _co_bm_v_##name = co::bm::add_group(#name, _co_bm_##name); \
    void _co_bm_##name(co::bm::Group& _g_)

// add a benchmark to the current group
#define BM_add(name) for (co::bm::Runner _r_(_g_, #name); !_g_.done(); _g_.goon())

// tell the compiler do not optimize this away
#define BM_use(v) co::bm::use(&v, sizeof(v));
