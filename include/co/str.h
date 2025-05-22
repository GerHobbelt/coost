#pragma once

#include "def.h"
#include "fastring.h"
#include "stl.h"

namespace str {

template<typename T>
inline fastring from(T t) {
    fastring s(24);
    s << t;
    return s;
}

bool to_bool(const char* s, int* err=nullptr);

int64 to_int64(const char* s, int* err=nullptr);

uint64 to_uint64(const char* s, int* err=nullptr);

int32 to_int32(const char* s, int* err=nullptr);

uint32 to_uint32(const char* s, int* err=nullptr);

double to_double(const char* s, int* err=nullptr);

fastring replace(
    const char* s, size_t n,
    const char* sub, size_t m,
    const char* to, size_t l,
    size_t t=0
);

inline fastring replace(const char* s, const char* sub, const char* to, size_t t=0) {
    return replace(s, strlen(s), sub, strlen(sub), to, strlen(to), t);
}

co::vector<fastring> split(const char* s, size_t n, char c, size_t t=0);

co::vector<fastring> split(const char* s, size_t n, const char* c, size_t m, size_t t=0);

// str::split("|x|y|", '|');    ->  [ "", "x", "y" ]
// str::split("xooy", 'o');     ->  [ "x", "", "y" ]
// str::split("xooy", 'o', 1);  ->  [ "x", "oy" ]
inline co::vector<fastring> split(const char* s, char c, size_t t=0) {
    return split(s, strlen(s), c, t);
}

inline co::vector<fastring> split(const fastring& s, char c, size_t t=0) {
    return split(s.data(), s.size(), c, t);
}

inline co::vector<fastring> split(const char* s, const char* c, size_t t=0) {
    return split(s, strlen(s), c, strlen(c), t);
}

inline co::vector<fastring> split(const fastring& s, const char* c, size_t t=0) {
    return split(s.data(), s.size(), c, strlen(c), t);
}

inline fastring trim(const char* s, const char* c=" \t\r\n", char d='b') {
    fastring x(s); x.trim(c, d); return x;
}

inline fastring trim(const char* s, char c, char d='b') {
    fastring x(s); x.trim(c, d); return x;
}

inline fastring trim(const fastring& s, const char* c=" \t\r\n", char d='b') {
    fastring x(s); x.trim(c, d); return x;
}

inline fastring trim(const fastring& s, char c, char d='b') {
    fastring x(s); x.trim(c, d); return x;
}

bool match(const char* s, size_t n, const char* p, size_t m);

} // str
