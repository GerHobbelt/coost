#include "co/url.h"

namespace co {

// - reserved:  ! ( ) * # $ & ' + , / : ; = ? @ [ ] 
// - a-z  A-Z  0-9  - _ . ~ 
static char g_tb[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

inline bool unencoded(uint8 c) { return g_tb[c]; }

inline int hex2int(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    return -1;
}

fastring url_encode(const void* s, size_t n) {
    fastring r(n + 32);
    const char* p = (const char*)s;

    char c;
    for (size_t i = 0; i < n; ++i) {
        c = p[i];
        if (unencoded(c)) {
            r.append(c);
            continue;
        }
        r.append('%');
        r.append("0123456789ABCDEF"[static_cast<uint8>(c) >> 4]);
        r.append("0123456789ABCDEF"[static_cast<uint8>(c) & 0x0F]);
    }

    return r;
}

fastring url_decode(const void* s, size_t n) {
    fastring r(n);
    const char* p = (const char*)s;

    for (size_t i = 0; i < n; ++i) {
        if (p[i] != '%') {
            r.append(p[i]);
            continue;
        }

        if (i + 2 >= n) goto err;       // invalid encode
        const int h4 = hex2int(p[i + 1]);
        const int l4 = hex2int(p[i + 2]);
        if (h4 < 0 || l4 < 0) goto err; // invalid encode

        r.append((char)((h4 << 4) | l4));
        i += 2;
    }

    return r;

err:
    return fastring();
}

} // co
