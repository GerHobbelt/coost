#pragma once

#include "def.h"
#include "god.h"
#include "dtoa.h"

namespace co {
namespace xx {

struct StrconvInit {
    StrconvInit();
    ~StrconvInit() = default;
};

static StrconvInit g_strconv_init;

} // xx

inline int dtoa(double v, char* buf, int mdp=324) {
    return milo::dtoa(v, buf, mdp);
}

// uint -> hex string (e.g. 255 -> 0xff)
int u32toh(uint32 v, char* buf);
int u64toh(uint64 v, char* buf);

int u32toa(uint32 v, char* buf);
int u64toa(uint64 v, char* buf);

inline int i32toa(int32 v, char* buf) {
    if (v >= 0) return u32toa((uint32)v, buf);
    *buf = '-';
    return u32toa((uint32)(-v), buf + 1) + 1;
}

inline int i64toa(int64 v, char* buf) {
    if (v >= 0) return u64toa((uint64)v, buf);
    *buf = '-';
    return u64toa((uint64)(-v), buf + 1) + 1;
}

template<typename V, god::if_t<sizeof(V) <= sizeof(int32), int> = 0>
inline int itoa(V v, char* buf) {
    return i32toa((int32)v, buf);
}

template<typename V, god::if_t<(sizeof(V) == sizeof(int64)), int> = 0>
inline int itoa(V v, char* buf) {
    return i64toa((int64)v, buf);
}

template<typename V, god::if_t<sizeof(V) <= sizeof(int32), int> = 0>
inline int utoa(V v, char* buf) {
    return u32toa((uint32)v, buf);
}

template<typename V, god::if_t<(sizeof(V) == sizeof(int64)), int> = 0>
inline int utoa(V v, char* buf) {
    return u64toa((uint64)v, buf);
}

#if __arch64
// pointer to hex string
inline int ptoh(const void* p, char* buf) {
    return u64toh((uint64)(size_t)p, buf);
}

#else
inline int ptoh(const void* p, char* buf) {
    return u32toh((uint32)(size_t)p, buf);
}
#endif

} // co
