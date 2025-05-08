#pragma once

#include "def.h"
#include "atomic.h"
#include <mutex>
#include <thread>

namespace co {
namespace xx {
extern __thread uint32 g_tid;
uint32 thread_id();
} // xx

typedef std::thread thread;
typedef std::mutex mutex;
typedef std::lock_guard<std::mutex> mutex_guard;

inline uint32 thread_id() {
    using xx::g_tid;
    return g_tid != 0 ? g_tid : (g_tid = xx::thread_id());
}

struct sync_event {
    explicit sync_event(bool manual_reset=false, bool signaled=false);
    ~sync_event();

    sync_event(sync_event&& e) noexcept : _p(e._p) { e._p = 0; }

    void signal();
    void reset();
    void wait();
    bool wait(uint32 ms);

    void* _p;
    DISALLOW_COPY_AND_ASSIGN(sync_event);
};

} // co
