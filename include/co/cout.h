#pragma once

#include "fastream.h"
#include <iostream>

namespace co {
namespace xx {

struct CoutInit {
    CoutInit();
    ~CoutInit() = default;
};

static CoutInit g_cout_init;

struct Print {
    Print();
    ~Print();
    fastream& s;
    size_t n;
};

enum Endl {
    endl,
};

} // xx

using xx::endl;

namespace color {

enum color_t {
    deflt = 0,
    red = 1,
    green = 2,
    yellow = 3,  // red | green
    blue = 4,
    magenta = 5, // blue | red
    cyan = 6,    // blue | green
    bold = 8,
    bright_red = 9,
    bright_green = 10,
    bright_yellow = 11,
    bright_blue = 12,
    bright_magenta = 13,
    bright_cyan = 14,
};

} // color

struct text {
    constexpr text(const char* s, color::color_t c) noexcept
        : s(s), c(c) {
    } 
    const char* s;
    color::color_t c;
};

} // co

namespace color = co::color;

std::ostream& operator<<(std::ostream&, color::color_t);
fastream& operator<<(fastream&, color::color_t);

inline std::ostream& operator<<(std::ostream& s, const co::text& t) {
    return s << t.c << t.s << color::deflt;
}

inline fastream& operator<<(fastream& s, const co::text& t) {
    return s << t.c << t.s << color::deflt;
}

inline std::ostream& operator<<(std::ostream& os, co::xx::Endl) {
    return (os << '\n').flush();
}

namespace co {

inline std::ostream& cout() { return std::cout; }

// print to stdout
template<typename X, typename ...V>
inline std::ostream& cout(X&& x, V&& ... v) {
    std::cout << std::forward<X>(x);
    return co::cout(std::forward<V>(v)...);
}

// print to stdout with newline (thread-safe)
template<typename ...X>
inline void print(X&& ...x) {
    xx::Print().s.cat(std::forward<X>(x)...);
}

} // co
