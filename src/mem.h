#pragma once

#include "co/def.h"
#include "co/clist.h"
#include <functional>

#ifndef _WIN32

#if __arch64
inline int _find_msb(size_t x) {
    return 63 - __builtin_clzll(x); // x != 0
}

inline uint32 _find_lsb(size_t x) {
    return __builtin_ffsll(x) - 1;
}

#else
inline int _find_msb(size_t x) {
    return 31 - __builtin_clz(x); // x != 0
}

inline uint32 _find_lsb(size_t x) {
    return __builtin_ffs(x) - 1;
}
#endif

inline uint32 _pow2_align(uint32 n) {
    return 1u << (32 - __builtin_clz(n - 1)); // n > 1
}

#else /* _WIN32 */
#include <intrin.h>

#if __arch64
inline int _find_msb(size_t x) {
    unsigned long i;
    _BitScanReverse64(&i, x);
    return (int)i;
}

inline uint32 _find_lsb(size_t x) {
    unsigned long r;
    _BitScanForward64(&r, x); // x != 0
    return r;
}

#else
inline int _find_msb(size_t x) {
    unsigned long i;
    _BitScanReverse(&i, x);
    return (int)i;
}

inline uint32 _find_lsb(size_t x) {
    unsigned long r;
    _BitScanForward(&r, x); // x != 0
    return r;
}
#endif

inline uint32 _pow2_align(uint32 n) {
    unsigned long r;
    _BitScanReverse(&r, n - 1);
    return 2u << r;
}

#endif

namespace co {

struct Memb : co::clink {
    char p[];
};

struct StaticMem {
    explicit StaticMem(uint32 blk_size)
        : _h(0), _pos(0), _blk_size(blk_size) {
    }

    ~StaticMem();

    void* alloc(uint32 n, uint32 align=sizeof(void*));

    union {
        Memb* _h;
        co::clist _l;
    };
    uint32 _pos;
    const uint32 _blk_size;
};

typedef std::function<void()> destruct_t;

struct Dealloc {
    static const uint32 A = alignof(destruct_t);
    static const uint32 N = sizeof(destruct_t);

    Dealloc() : _m(8192) {}
    ~Dealloc();

    void add_destructor(destruct_t&& d) {
        const auto p = _m.alloc(N, A);
        new(p) destruct_t(std::forward<destruct_t>(d));
    }
    
    StaticMem _m;
};

} // co
