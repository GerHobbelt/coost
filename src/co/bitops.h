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

namespace bit {

inline int find_1(uint64 x) {
    return _find_lsb(x);
}

inline int find_0(uint64 x) {
    return find_1(~x);
}

inline void set(uint64& x, int i) {
    x |= (1ull << i);
}

inline uint64 set_and_fetch(uint64& x, int i) {
    return x |= (1ull << i);
}

inline void unset(uint64& x, int i) {
    x &= ~(1ull << i);
}

inline uint64 fetch_and_unset(uint64& x, int i) {
    const uint64 o = x;
    x &= ~(1ull << i);
    return o;
}

inline uint64 unset_and_fetch(uint64& x, int i) {
    return x &= ~(1ull << i);
}

} // bit
