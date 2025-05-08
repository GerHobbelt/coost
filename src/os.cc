#include "co/os.h"

#ifndef _WIN32
#include <stdio.h>  // popen, pclose
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace os {

fastring env(const char* name) {
    char* x = ::getenv(name);
    return x ? fastring(x) : fastring();
}

bool env(const char* name, const char* value) {
    if (value && *value) return ::setenv(name, value, 1) == 0;
    return ::unsetenv(name) == 0;
}

fastring homedir() {
    return os::env("HOME");
}

fastring cwd() {
    fastring s(128);
    while (true) {
        if (::getcwd(s.data(), s.capacity())) {
            s.resize(strlen(s.data()));
            return s;
        }
        if (errno != ERANGE) return fastring();
        s.reserve(s.capacity() << 1);
    }
}

#ifdef __APPLE__
fastring exepath() {
    fastring s(128);
    uint32_t n = 128;
    while (true) {
        if (_NSGetExecutablePath(s.data(), &n) == 0) {
            s.resize(strlen(s.data()));
            return s;
        }
        s.reserve(n);
    }
}

#else
fastring exepath() {
    fastring s(128);
    while (true) {
        auto r = readlink("/proc/self/exe", s.data(), s.capacity());
        if (r < 0) return fastring();
        if ((size_t)r != s.capacity()) {
            s.resize(r);
            return s;
        }
        s.reserve(s.capacity() << 1);
    }
}
#endif

fastring exedir() {
    fastring s = os::exepath();
    size_t n = s.rfind('/');
    if (n != s.npos) {
        if (n != 0) {
            s[n] = '\0';
            s.resize(n);
        } else {
            if (s.capacity() > 1) s[1] = '\0';
            s.resize(1);
        }
    }
    return s;
}

fastring exename() {
    fastring s = os::exepath();
    return s.substr(s.rfind('/') + 1);
}

int pid() {
    return (int) getpid();
}

int cpunum() {
    return (int) sysconf(_SC_NPROCESSORS_ONLN);
}

size_t pagesize() {
    return (size_t) sysconf(_SC_PAGESIZE);
}
sig_handler_t signal(int sig, sig_handler_t handler, int flag) {
    struct sigaction sa, old;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    if (flag > 0) sa.sa_flags = flag;
    sa.sa_handler = handler;
    int r = sigaction(sig, &sa, &old);
    return r == 0 ? old.sa_handler : SIG_ERR;
}

bool system(const char* cmd) {
    FILE* f = popen(cmd, "w");
    return f ? pclose(f) != -1 : false;
}

} // os

#else
#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace os {

fastring env(const char* name) {
    fastring s(64);
    DWORD r = GetEnvironmentVariableA(name, s.data(), 64);
    s.resize(r);
    if (r > 64) {
        GetEnvironmentVariableA(name, (char*)s.data(), r);
        s.resize(r - 1);
    }
    return s;
}

bool env(const char* name, const char* value) {
    return SetEnvironmentVariableA(name, value) == TRUE;
}

inline void backslash_to_slash(fastring& s) {
    std::for_each((char*)s.data(), (char*)s.data() + s.size(), [](char& c){
        if (c == '\\') c = '/';
    });
}

fastring homedir() {
    fastring s = os::env("USERPROFILE"); // SYSTEMDRIVE + HOMEPATH
    backslash_to_slash(s);
    return s;
}

fastring cwd() {
    fastring s(64);
    DWORD r = GetCurrentDirectoryA(64, (char*)s.data());
    s.resize(r);
    if (r > 64) {
        GetCurrentDirectoryA(r, (char*)s.data());
        s.resize(r - 1);
    }
    if (!(s.size() > 1 && s[0] == '\\' && s[1] == '\\')) backslash_to_slash(s);
    return s;
}

static fastring _get_module_path() {
    DWORD n = 128, r = 0;
    fastring s(128);
    while (true) {
        r = GetModuleFileNameA(NULL, (char*)s.data(), n);
        if (r < n) { s.resize(r); break; }
        n <<= 1;
        s.reserve(n);
    }
    return s;
}

fastring exepath() {
    fastring s = _get_module_path();
    if (!(s.size() > 1 && s[0] == '\\' && s[1] == '\\')) backslash_to_slash(s);
    return s;
}

fastring exedir() {
    fastring s = _get_module_path();
    size_t n = s.rfind('\\');
    if (n != s.npos && n != 0) {
        if (s[n - 1] != ':') {
            s[n] = '\0';
            s.resize(n);
        } else {
            s.resize(n + 1);
            if (s.capacity() > n + 1) s[n + 1] = '\0';
        }
    }

    if (!(s.size() > 1 && s[0] == '\\' && s[1] == '\\')) backslash_to_slash(s);
    return s;
}

fastring exename() {
    fastring s = _get_module_path();
    return s.substr(s.rfind('\\') + 1);
}

int pid() {
    return (int) GetCurrentProcessId();
}

int cpunum() {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (int) info.dwNumberOfProcessors;
}

size_t pagesize() {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (size_t) info.dwPageSize;
}

sig_handler_t signal(int sig, sig_handler_t handler, int) {
    return ::signal(sig, handler);
}

bool system(const char* cmd) {
    return ::system(cmd) != -1;
}

} // os

#endif
