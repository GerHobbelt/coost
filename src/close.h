#pragma once

#ifndef _WIN32
#include <unistd.h>

#if defined(__linux__)
#include <sys/syscall.h>

#ifndef SYS_close
#define SYS_close __NR_close
#endif

// see https://www.man7.org/linux/man-pages/man2/close.2.html
inline int _close(int fd) {
    return syscall(SYS_close, fd);
}

#elif defined(_hpux) || defined(__hpux)
#include <errno.h>

inline int _close(int fd) {
    int r;
    while ((r = ::close(fd)) != 0 && errno == EINTR);
    return r;
}

#else
inline int _close(int fd) {
    return ::close(fd);
}
#endif
#endif
