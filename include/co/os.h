#pragma once

#include "fastring.h"
#include <signal.h>

namespace os {

// get value of environment variable
fastring env(const char* name);

// set value of environment variable
bool env(const char* name, const char* value);

// get home dir of current user
fastring homedir();

// current working directory
fastring cwd();

// executable path
fastring exepath();

// executable directory
fastring exedir();

// executable name
fastring exename();

// current process id
int pid();

// number of CPU cores
int cpunum();

// get size of a page in bytes
size_t pagesize();

typedef void (*sig_handler_t)(int);

// set signal handler, return the old handler
sig_handler_t signal(int sig, sig_handler_t handler, int flag=0);

// execute a shell command
bool system(const char* cmd);

inline bool system(const fastring& cmd) {
    return os::system(cmd.c_str());
}

} // namespace os
