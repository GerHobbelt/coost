#pragma once

#include "def.h"
#include "closure.h"
#include "sock.h"

namespace co {
namespace xx {

struct CoInit {
    CoInit();
    ~CoInit();
};

static CoInit g_co_init;

} // xx

void go(co::closure* c);

template<typename F>
inline void go(F&& f) {
    go(co::closure::create(std::forward<F>(f)));
}

template<typename F, typename P>
inline void go(F&& f, P&& p) {
    go(co::closure::create(std::forward<F>(f), std::forward<P>(p)));
}

template<typename F, typename T, typename P>
inline void go(F&& f, T* t, P&& p) {
    go(co::closure::create(std::forward<F>(f), t, std::forward<P>(p)));
}

typedef void coro_t;
typedef void sched_t;

// get number of schedulers
int sched_num();

// get the current scheduler, NULL if called from non-scheduler thread
sched_t* sched();

// get the current coroutine
coro_t* coroutine();

// return id of the current scheduler, or -1 if called from non-scheduler thread
int sched_id();

// return id of the current coroutine, or -1 if called from non-coroutine
int coroutine_id();

// suspend the current coroutine 
void yield();

// resume a coroutine, @co is the result of co::coroutine()
void resume(coro_t* co);

// sleep for milliseconds in coroutine
void sleep(uint32 ms);

// check whether the current coroutine has timed out 
bool timeout();

// check whether the memory @p points to is on the stack of the current coroutine 
bool on_stack(const void* p);

// add a timer for the current coroutine 
void add_timer(uint32 ms);

// add an IO event to the socket
void add_io_event(sock_t fd, ev_t ev);

// remove an IO event from the socket
void del_io_event(sock_t fd, ev_t ev);

// remove all IO events from the socket 
void del_io_event(sock_t fd);

// mutex lock for coroutines
struct cutex {
    cutex();
    ~cutex();

    cutex(cutex&& c) noexcept : _p(c._p) { c._p = 0; }

    // copy constructor, increment the reference count only
    cutex(const cutex& c);

    void operator=(const cutex&) = delete;

    void lock() const;

    void unlock() const;

    bool try_lock() const;

    void* _p;
};

struct cutex_guard {
    explicit cutex_guard(const cutex& c) : _c(c) {
        _c.lock();
    }

    explicit cutex_guard(const co::cutex* m) : _c(*m) {
        _c.lock();
    }

    ~cutex_guard() {
        _c.unlock();
    }

    const co::cutex& _c;
    DISALLOW_COPY_AND_ASSIGN(cutex_guard);
};

// for communications between coroutines and/or threads
struct event {
    explicit event(bool manual_reset=false, bool signaled=false);
    ~event();

    event(event&& e) noexcept : _p(e._p) {
        e._p = 0;
    }

    // copy constructor, increment the reference count only
    event(const event& e);

    void operator=(const event&) = delete;

    void wait() const {
        (void) this->wait((uint32)-1);
    }

    // return false if timedout
    bool wait(uint32 ms) const ;

    void signal() const;

    void reset() const;

    void* _p;
};

struct wait_group {
    explicit wait_group(uint32 n);

    wait_group() : wait_group(0) {}

    ~wait_group();

    wait_group(wait_group&& wg) noexcept : _p(wg._p) {
        wg._p = 0;
    }

    // copy constructor, increment the reference count only
    wait_group(const wait_group& wg);

    void operator=(const wait_group&) = delete;

    // increase the counter by n (1 by default)
    void add(uint32 n=1) const;

    // decrease the counter by 1
    void done() const;

    // blocks until the counter becomes 0
    void wait() const;

    void* _p;
};

// pool used in coroutines
struct pool {
    typedef std::function<void*()> create_cb_t;
    typedef std::function<void(void*)> destroy_cb_t;

    pool();
    ~pool();

    // @c    callback used to create an element
    //       eg.  []() { return (void*) new T; }
    // @d    callback used to destroy an element
    //       eg.  [](void* p) { delete (T*)p; }
    // @cap  max capacity of the pool per thread
    pool(create_cb_t&& c, destroy_cb_t&& d, uint32 cap=(uint32)-1);

    pool(pool&& p) : _p(p._p) { p._p = 0; }

    pool(const pool& p);

    void operator=(const pool&) = delete;

    // pop an element from the pool of the current thread 
    void* pop() const;

    // push an element to the pool of the current thread 
    void push(void* e) const;

    void* _p;
};

// hold a pointer from co::pool, and push it back when destructed
template<typename T>
struct pooled_ptr {
    explicit pooled_ptr(const pool& p) : _p(p) {
        _e = (T*)_p.pop();
    }

    explicit pooled_ptr(const pool* p) : pooled_ptr(*p) {}

    ~pooled_ptr() { _p.push(_e); }

    T* operator->() const { assert(_e); return _e; }
    T& operator*()  const { assert(_e); return *_e; }

    bool operator==(T* e) const noexcept { return _e == e; }
    bool operator!=(T* e) const noexcept { return _e != e; }
    explicit operator bool() const noexcept { return _e != nullptr; }

    T* get() const noexcept { return _e; }

    const pool& _p;
    T* _e;
    DISALLOW_COPY_AND_ASSIGN(pooled_ptr);
};

struct crontab {
    crontab() : _stopped(false) {}
    ~crontab() = default;

    // run f() in @ms miliseconds
    template<typename F>
    void run_in(F&& f, int ms) {
        go([f, ms]() {
            co::sleep(ms);
            f();
        });
    }

    // run f() every @ms miliseconds
    template<typename F>
    void run_every(F&& f, int ms) {
        _wg.add();
        go([this, f, ms]() {
            while (!this->_stopped) {
                co::sleep(ms);
                f();
            }
            _wg.done();
        });
    }

    void stop() {
        atomic_store(&_stopped, true, mo_relaxed);
        _wg.wait();
    }

    co::wait_group _wg;
    bool _stopped;
};

} // co

using co::go;
