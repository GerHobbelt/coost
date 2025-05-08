#pragma once

#include <string.h>

namespace co {

char* memrchr(const char* s, char c, size_t n);
char* memmem(const char* s, size_t n, const char* p, size_t m);
char* memimem(const char* s, size_t n, const char* p, size_t m);
char* memrmem(const char* s, size_t n, const char* p, size_t m);

inline int memcmp(const char* s, size_t n, const char* p, size_t m) {
    const int i = ::memcmp(s, p, n < m ? n : m);
    return i != 0 ? i : (n < m ? -1 : n != m);
}

} // co
