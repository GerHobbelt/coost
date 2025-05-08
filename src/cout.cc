#include "co/cout.h"
#include <ios>
#include <mutex>

#ifdef _WIN32
#ifdef _MSC_VER
#pragma warning(disable:4503)
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

static const char* fg[16] = {
    "\033[0m",   // default
    "\033[31m",  // red
    "\033[32m",  // green
    "\033[33m",  // yellow
    "\033[34m",  // blue
    "\033[35m",  // magenta
    "\033[36m",  // cyan
    "\033[37m",  // white
    "\033[1m",   // bold
    "\033[1m\033[91m",
    "\033[1m\033[32m",
    "\033[1m\033[33m",
    "\033[1m\033[94m",
    "\033[1m\033[95m",
    "\033[1m\033[96m",
    "\033[1m\033[97m",
};

#ifdef _WIN32
static bool g_has_vterm;

static void cinit() {
    auto h = GetStdHandle(STD_OUTPUT_HANDLE);
    g_has_vterm = []() {
        char buf[128];
        DWORD r = GetEnvironmentVariableA("TERM", buf, 128);
        if (r != 0) return true;
    #ifdef ENABLE_VIRTUAL_TERMINAL_PROCESSING
        DWORD mode = 0;
        if (h && GetConsoleMode(h, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            if (SetConsoleMode(h, mode)) return true;
        }
    #endif
        return false;
    }();
}

std::ostream& operator<<(std::ostream& os, color::color_t c) {
    if (g_has_vterm) return os << fg[c];
    return os;
}

fastream& operator<<(fastream& s, color::color_t c) {
    if (g_has_vterm) s << fg[c];
    return s;
}

#else
inline void cinit() {}

std::ostream& operator<<(std::ostream& os, color::color_t c) {
    return os << fg[c];
}

fastream& operator<<(fastream& s, color::color_t c) {
    return s << fg[c];
}
#endif

namespace co {
namespace xx {

static std::mutex* g_m;
static __thread fastream* g_s;

static int g_nifty_counter;

CoutInit::CoutInit() {
    if (g_nifty_counter++ == 0) {
        cinit();
        std::cout << std::boolalpha;
        g_m = co::_make_rootic<std::mutex>();
    }
}

inline fastream& _stream() {
    return g_s ? *g_s : *(g_s = co::_make_rootic<fastream>(256));
}

Print::Print() : s(_stream()) {
    n = s.size();
    if (n == 0 && s.capacity() > 8192) s.swap(fastream(8192));
}

Print::~Print() {
    s << '\n';
    {
        std::lock_guard<std::mutex> m(*g_m);
        ::fwrite(s.data() + n, 1, s.size() - n, stdout);
    }
    s.resize(n);
}

} // xx
} // co
