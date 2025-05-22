#include "co/str.h"
#include "co/def.h"
#include <stdlib.h>
#include <string.h>

namespace str {

bool to_bool(const char* s, int* e) {
    if (e) *e = 0;
    if (strcmp(s, "false") == 0 || strcmp(s, "0") == 0) return false;
    if (strcmp(s, "true") == 0 || strcmp(s, "1") == 0) return true;
    if (e) *e = EINVAL;
    return false;
}

inline int _shift(char c) {
    switch (c) {
        case 'k':
        case 'K':
            return 10;
        case 'm':
        case 'M':
            return 20;
        case 'g':
        case 'G':
            return 30;
        case 't':
        case 'T':
            return 40;
        case 'p':
        case 'P':
            return 50;
        default:
            return 0;
    }
}

int64 to_int64(const char* s, int* e) {
    errno = 0;
    if (e) *e = 0;

    char* end = 0;
    int64 x = strtoll(s, &end, 0);
    if (errno != 0) {
        if (e) *e = errno;
        return 0;
    }

    size_t n = strlen(s);
    if (end == s + n) return x;

    if (end == s + n - 1) {
        int shift = _shift(s[n - 1]);
        if (shift != 0) {
            if (x == 0) return 0;
            if (x < (MIN_INT64 >> shift) || x > (MAX_INT64 >> shift)) {
                if (e) *e = ERANGE;
                return 0;
            }
            return x << shift;
        }
    }

    if (e) *e = EINVAL;
    return 0;
}

uint64 to_uint64(const char* s, int* e) {
    errno = 0;
    if (e) *e = 0;

    char* end = 0;
    uint64 x = strtoull(s, &end, 0);
    if (errno != 0) {
        if (e) *e = errno;
        return 0;
    }

    size_t n = strlen(s);
    if (end == s + n) return x;

    if (end == s + n - 1) {
        int shift = _shift(s[n - 1]);
        if (shift != 0) {
            if (x == 0) return 0;
            int64 absx = (int64)x;
            if (absx < 0) absx = -absx;
            if (absx > static_cast<int64>(MAX_UINT64 >> shift)) {
                if (e) *e = ERANGE;
                return 0;
            }
            return x << shift;
        }
    }

    if (e) *e = EINVAL;
    return 0;
}

int32 to_int32(const char* s, int* e) {
    const int64 x = to_int64(s, e);
    if (MIN_INT32 <= x && x <= MAX_INT32)  return (int32)x;
    if (e) *e = ERANGE;
    return 0;
}

uint32 to_uint32(const char* s, int* e) {
    const int64 x = (int64) to_uint64(s, e);
    const int64 absx = x < 0 ? -x : x;
    if (absx <= MAX_UINT32) return (uint32)x;
    if (e) *e = ERANGE;
    return 0;
}

double to_double(const char* s, int* e) {
    errno = 0;
    if (e) *e = 0;
    char* end = 0;
    double x = strtod(s, &end);
    if (errno != 0) {
        if (e) *e = errno;
        return 0;
    }

    if (end == s + strlen(s)) return x;
    if (e) *e = EINVAL;
    return 0;
}

bool match(const char* s, size_t n, const char* p, size_t m) {
    char c;
    while (n > 0 && m > 0 && (c = p[m - 1]) != '*') {
        if (c != s[n - 1] && c != '?') return false;
        --n, --m;
    }
    if (m == 0) return n == 0;

    size_t si = 0, pi = 0, sl = -1, pl = -1;
    while (si < n && pi < m) {
        c = p[pi];
        if (c == '*') {
            sl = si;
            pl = ++pi;
            continue;
        }

        if (c == s[si] || c == '?') {
            ++si, ++pi;
            continue;
        }

        if (sl != (size_t)-1 && sl + 1 < n) {
            si = ++sl;
            pi = pl;
            continue;
        }

        return false;
    }

    while (pi < m) {
        if (p[pi++] != '*') return false;
    }
    return true;
}

fastring replace(
    const char* s, size_t n,
    const char* sub, size_t m,
    const char* to, size_t l,
    size_t t
) {
    if (m == 0) return fastring(s, n);

    const char* p;
    const char* const end = s + n;
    fastring x(n);

    while ((p = co::memmem(s, end - s, sub, m))) {
        x.append(s, p - s).append(to, l);
        s = p + m;
        if (t && --t == 0) break;
    }

    if (s < end) x.append(s, end - s);
    return x;
}

co::vector<fastring> split(const char* s, size_t n, char c, size_t t) {
    co::vector<fastring> v;
    v.reserve(8);

    const char* p;
    const char* const end = s + n;

    while ((p = (const char*) ::memchr(s, c, end - s))) {
        v.emplace_back(s, p - s);
        s = p + 1;
        if (v.size() == t) break;
    }

    if (s < end) v.emplace_back(s, end - s);
    return v;
}

co::vector<fastring> split(const char* s, size_t n, const char* c, size_t m, size_t t) {
    co::vector<fastring> v;
    if (m == 0) return v;
    v.reserve(8);

    const char* p;
    const char* const end = s + n;

    while ((p = co::memmem(s, end - s, c, m))) {
        v.emplace_back(s, p - s);
        s = p + m;
        if (v.size() == t) break;
    }

    if (s < end) v.emplace_back(s, end - s);
    return v;
}

} // str
