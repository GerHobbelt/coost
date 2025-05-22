#pragma once

#include "co/def.h"

#if __arch32
#error "32-bit platform not supported"
#endif

#ifdef _WIN32
#include <intrin.h>

inline int _find_lsb(uint64 x) {
    unsigned long r;
    return _BitScanForward64(&r, x) != 0 ? (int)r : -1;
}

#else
inline int _find_lsb(uint64 x) {
    return __builtin_ffsll(x) - 1;
}
#endif

namespace co {

struct Bitset {
    int find_zero() {
        return _find_lsb(~_b);
    }

    void set(int i) {
        _b |= (1ull << i);
    }

    uint64 set_and_fetch(int i) {
        return _b |= (1ull << i);
    }

    void unset(int i) {
        _b &= ~(1ull << i);
    }

    uint64 fetch_and_unset(int i) {
        const uint64 o = _b;
        _b &= ~(1ull << i);
        return o;
    }

    uint64 unset_and_fetch(int i) {
        return _b &= ~(1ull << i);
    }

    uint64 _b;
};

} // co
