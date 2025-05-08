#include "co/search.h"
#include <ctype.h>
#include <stdint.h>
#include <limits.h>

namespace co {

inline bool _has_null(size_t x) {
    const size_t o = (size_t)-1 / 255;
    return (x - o) & ~x & (o * 0x80);
}

char* memrchr(const char* s, char c, size_t n) {
    if (n == 0) return nullptr;

    char* p = (char*)s + n - 1;
    while ((size_t)(p + 1) & (sizeof(size_t) - 1)) {
        if (*p == c) return p;
        if (p-- == s) return nullptr;
    }

    if (p - s >= sizeof(size_t) - 1) {
        const size_t mask = (size_t)-1 / 255 * (unsigned char)c;
        size_t* w = (size_t*)(p - (sizeof(size_t) - 1));
        do {
            if (_has_null(*w ^ mask)) break;
            --w;
        } while ((char*)w >= s);
        p = (char*)w + (sizeof(size_t) - 1);
    }

    while (p >= s) {
        if (*p == c) return p;
        --p;
    }
    return nullptr;
}

#define RETURN_TYPE void*
#define AVAILABLE(h, h_l, j, n_l) ((j) <= (h_l) - (n_l))
#include "two_way.h"

char* memmem(const char* s, size_t n, const char* p, size_t m) {
    if (n < m) return NULL;
    if (n == 0 || m == 0) return (char*)s;

    typedef unsigned char* S;
    if (m < LONG_NEEDLE_THRESHOLD) {
        const char* const b = s;
        s = (const char*) memchr(s, *p, n);
        if (!s || m == 1) return (char*)s;

        n -= s - b; 
        return n < m ? NULL : (char*)two_way_short_needle((S)s, n, (S)p, m);
    }

    return (char*)two_way_long_needle((S)s, n, (S)p, m);
}

static int _memicmp(const void* s, const void* t, size_t n) {
    const unsigned char* p = (const unsigned char*)s;
    const unsigned char* q = (const unsigned char*)t;
    int d = 0;
    for (; n != 0; --n) {
        if ((d = ::tolower(*p++) - ::tolower(*q++)) != 0) break;
    }
    return d;
}

#define RETURN_TYPE void*
#define AVAILABLE(h, h_l, j, n_l) ((j) <= (h_l) - (n_l))
#define FN_NAME(x) x##_i
#define CANON_ELEMENT(c) ::tolower(c)
#define CMP_FUNC _memicmp
#include "two_way.h"

char* memimem(const char* s, size_t n, const char* p, size_t m) {
    if (n < m) return NULL;
    if (n == 0 || m == 0) return (char*)s;

    typedef unsigned char* S;
    if (m < LONG_NEEDLE_THRESHOLD) {
        return (char*)two_way_short_needle_i((S)s, n, (S)p, m);
    }
    return (char*)two_way_long_needle_i((S)s, n, (S)p, m);
}

char* memrmem(const char* s, size_t n, const char* p, size_t m) {
    if (n < m) return nullptr;
    if (m == 0) return (char*)(s + n);

    const char* const e = co::memrchr(s, *(p + m - 1), n);
    if (!e || m == 1) return (char*)e;
    if (e - s + 1 < m) return nullptr;

    size_t off[256] = { 0 };
    for (size_t i = m; i > 0; --i) off[(unsigned char)p[i - 1]] = i;

    for (const char* b = e - m + 1;;) {
        if (::memcmp(b, p, m) == 0) return (char*)b;
        if (b == s) return nullptr;

        size_t o = off[(unsigned char)*(b - 1)];
        if (o == 0) o = m + 1;
        if (b < s + o) return nullptr;
        b -= o;
    }
}

} // co
