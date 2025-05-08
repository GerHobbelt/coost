#pragma once

#include "bitops.h"
#ifdef _MSC_VER
#pragma warning (disable:4127)
#endif

#include "../dlog.h"
#include "idgen.h"
#include "buffer.h"
#include "co/clist.h"
#include "co/god.h"
#include "co/mem.h"
#include "co/flag.h"
#include "co/stl.h"
#include "co/rand.h"
#include "co/time.h"
#include "co/thread.h"
#include "co/closure.h"
#include "co/fastream.h"
#include "context/context.h"

#if defined(_WIN32)
#include "epoll/iocp.h"
#elif defined(__linux__)
#include "epoll/epoll.h"
#else
#include "epoll/kqueue.h"
#endif

DEC_uint32(co_sched_num);
DEC_uint32(co_stack_num);
DEC_uint32(co_stack_size);

namespace co {

extern uint32 g_sched_num;
extern uint32 g_stack_num;
extern uint32 g_stack_size;

struct Coroutine;
struct Sched;

extern __thread Sched* g_sched;
extern co::vector<Sched*>* g_all_scheds;

typedef co::multimap<int64, Coroutine*>::iterator timer_id_t;

inline fastream& operator<<(fastream& fs, const timer_id_t& id) {
    return fs << *(void**)(&id);
}

enum state_t : uint8 {
    st_wait = 0,    // wait for an event, do not modify
    st_ready = 1,   // ready to resume
    st_timeout = 2, // timeout
};

// waiting context, for co::event
struct Waitx {
    Waitx* next;
    Waitx* prev;
    Coroutine* co;
    union { uint8 state; void* dummy; };

    static Waitx* create(void* co) {
        Waitx* w = (Waitx*) co::alloc(sizeof(Waitx)); assert(w);
        w->next = w->prev = 0;
        w->co = (Coroutine*)co;
        w->state = st_wait;
        static_assert(st_wait == 0, "");
        return w;
    }
};

// offsetof(Coroutine, wtx) == offsetof(Waitx, co)
struct Coroutine {
    Coroutine() = delete;
    ~Coroutine() = delete;

    union {
        Coroutine* next;
        co::closure* cb;  // coroutine function
    };
    Coroutine* prev;

    Waitx* wtx;       // waiting context
    Sched* sched;     // scheduler this coroutine runs in
    int32 id;         // coroutine id
    tb_context_t ctx; // coroutine context, points to the stack bottom
    union {
        Buffer buf;   // for saving stack data of this coroutine
        void* pbuf;
    };
    union { timer_id_t it;  char _dummy2[sizeof(timer_id_t)]; };
};

struct CoroutinePool {
    static const size_t B = 4096;
    static const int N = (B - 64) / sizeof(Coroutine);
    static const uint64 F = ((uint64)-1) >> (64 - N);

    CoroutinePool() : _h(nullptr) {}
    ~CoroutinePool() = default;

    struct Mem : co::clink {
        uint64 b;
        Coroutine* c;

        static Mem* create() {
            static_assert(N < 64, "");
            Mem* m = (Mem*) co::zalloc(B);
            m->c = (Coroutine*) ((char*)m + 64);
            return m;
        }
    };

    Coroutine* pop() {
        if (_h && _h->b < F) goto _1;
        _c.push_front(Mem::create());

    _1:
        int i = bit::find_0(_h->b);
        Coroutine* c = _h->c + i;
        if (bit::set_and_fetch(_h->b, i) < F) goto _end;
        _c.move_back(_h);

    _end:
        return c;
    }

    void push(Coroutine* c) {
        Mem* m = (Mem*) god::align_down<B>(c);
        const int i = (int) (c - m->c);
        if (bit::unset_and_fetch(m->b, i) != 0) goto _1;

        _c.erase(m);
        co::free(m, B);
        goto _end;

    _1:
        if (_h->b < F) goto _end;
        _c.move_front(m);

    _end:
        return;
    }

    union {
        Mem* _h;
        co::clist _c;
    };
};

struct alignas(co::cache_line_size) TaskManager {
    TaskManager() : _mtx() {
        _new_tasks.reserve(512);
        _ready_tasks.reserve(512);
    }
    ~TaskManager() = default;

    void add_new_task(co::closure* cb) {
        std::lock_guard<std::mutex> g(_mtx);
        _new_tasks.push_back(cb);
    }

    void add_ready_task(Coroutine* co) {
        std::lock_guard<std::mutex> g(_mtx);
        _ready_tasks.push_back(co);
    }

    void get_all_tasks(
        co::vector<co::closure*>& new_tasks,
        co::vector<Coroutine*>& ready_tasks
    ) {
        std::lock_guard<std::mutex> g(_mtx);
        if (!_new_tasks.empty()) _new_tasks.swap(new_tasks);
        if (!_ready_tasks.empty()) _ready_tasks.swap(ready_tasks);
    }

    size_t steal_tasks(co::vector<co::closure*>& v) {
        std::lock_guard<std::mutex> g(_mtx);
        if (!_new_tasks.empty()) {
            const size_t r = _new_tasks.size() >> 1;
            const size_t s = _new_tasks.size() - r; // stealed
            v.reserve(s);
            ::memcpy(v.data(), &_new_tasks[r], s * sizeof(co::closure*));
            _new_tasks.resize(r);
            return s;
        }
        return 0;
    }

    std::mutex _mtx;
    co::vector<co::closure*> _new_tasks;
    co::vector<Coroutine*> _ready_tasks;
};

struct TimerManager {
    TimerManager() : _timer(), _it(_timer.end()) {}
    ~TimerManager() = default;

    timer_id_t add_timer(uint32 ms, Coroutine* co) {
        return _it = _timer.emplace_hint(_it, time::mono.ms() + ms, co);
    }

    void del_timer(const timer_id_t& it) {
        if (_it == it) ++_it;
        _timer.erase(it);
    }

    timer_id_t end() { return _timer.end(); }

    // get timedout coroutines, return time(ms) to wait for the next timeout
    uint32 check_timeout(co::vector<Coroutine*>& res);

    co::multimap<int64, Coroutine*> _timer;        // timed-wait tasks: <time_ms, co>
    co::multimap<int64, Coroutine*>::iterator _it; // make insert faster with this hint
};

struct Stack {
    char* p;       // stack pointer 
    char* top;     // stack top
    Coroutine* co; // coroutine owns this stack
};

// coroutine scheduler, loop in a single thread
struct Sched {
    Sched(uint32 id);
    ~Sched();

    uint32 id() const { return _id; }

    // the current running coroutine
    Coroutine* running() const { return _running; }

    // id of the current running coroutine
    int coroutine_id() const {
        return g_sched_num * (_running->id - 1) + _id;
    }

    Stack* get_stack(uint32 co_id) const {
        return &_stack[co_id & (g_stack_num - 1)];
    }

    bool on_stack(const void* p) const {
        Stack* const s =  this->get_stack(_running->id);
        return (s->p <= (char*)p) && ((char*)p < s->top);
    }

    void resume(void* c);

    // suspend the current coroutine
    void yield() {
        tb_context_jump(_main_co->ctx, _running);
    }

    void add_new_task(co::closure* cb) {
        _task_mgr.add_new_task(cb);
        _epoll.signal();
    }

    void add_ready_task(Coroutine* co) {
        _task_mgr.add_ready_task(co);
        _epoll.signal();
    }

    // steal tasks from other scheduler
    size_t steal_tasks() {
        const uint32 r = co::rand(_seed) % (g_sched_num - 1);
        const uint32 k = r < _id ? r : r + 1;
        _new_tasks.clear();
        const size_t n = (*g_all_scheds)[k]->_task_mgr.steal_tasks(_new_tasks);
        CO_DLOG("sched(", _id, ") steal ", n, " tasks from sched(", k, ")");
        return n;
    }

    void sleep(uint32 ms) {
        if (_wait_ms > ms) _wait_ms = ms;
        (void) _timer_mgr.add_timer(ms, _running);
        this->yield();
    }

    void add_timer(uint32 ms) {
        if (_wait_ms > ms) _wait_ms = ms;
        _running->it = _timer_mgr.add_timer(ms, _running);
        CO_DLOG("co(", _running, ") add timer ", _running->it, " (", ms, " ms)");
    }

    bool timeout() const { return _timeout; }

    bool add_ev_read(sock_t fd) {
        CO_DLOG("co(", _running, ") add ev_read, fd: ", fd);
        return _epoll.add_ev_read(fd, _running);
    }

    bool add_ev_write(sock_t fd) {
        CO_DLOG("co(", _running, ") add ev_write, fd: ", fd);
        return _epoll.add_ev_write(fd, _running);
    }

    void del_ev_read(sock_t fd) {
        CO_DLOG("co(", _running, ") del ev_read, fd: ", fd);
        _epoll.del_ev_read(fd);
    }

    void del_ev_write(sock_t fd) {
        CO_DLOG("co(", _running, ") del ev_write, fd: ", fd);
        _epoll.del_ev_write(fd);
    }

    bool add_io_event(sock_t fd, ev_t ev) {
        return ev == ev_read ? add_ev_read(fd) : add_ev_write(fd);
    }

    void del_io_event(sock_t fd, ev_t ev) {
        ev == ev_read ? del_ev_read(fd) : del_ev_write(fd);
    }

    // delete all IO events on a socket from the epoll.
    void del_io_event(sock_t fd) {
        CO_DLOG("co(", _running, ") del io event, fd: ", fd);
        _epoll.del_event(fd);
    }

    void start() { std::thread(&Sched::loop, this).detach(); }

    void stop();

    void loop();

    // entry function for coroutine
    static void main_func(tb_context_from_t from);

    void save_stack(Coroutine* co) {
        if (co) {
            co->buf.clear();
            co->buf.append(co->ctx, co->sched->get_stack(co->id)->top - (char*)co->ctx);
        }
    }

    Coroutine* new_coroutine(co::closure* cb) {
        Coroutine* c = _co_pool.pop();
        c->cb = cb;
        if (!c->sched) c->sched = this;
        c->id = _idgen.pop();
        new(&c->it) timer_id_t(_timer_mgr.end());
        return c;
    }

    void free_coroutine(Coroutine* c) {
        c->it.~timer_id_t();
        c->buf.reset();
        c->ctx = nullptr;
        _idgen.push(c->id);
        _co_pool.push(c);
    }

    Epoll _epoll;
    TaskManager _task_mgr;
    TimerManager _timer_mgr;

    uint32 _id; // scheduler id
    uint32 _seed;
    co::vector<co::closure*> _new_tasks;
    co::vector<Coroutine*> _ready_tasks;

    IdGen _idgen;
    CoroutinePool _co_pool;
    co::sync_event _ev;
    bool _stopped;
    bool _timeout;
    uint32 _wait_ms;     // time the epoll to wait for
    Coroutine* _running; // the current running coroutine
    Coroutine* _main_co; // save the main context
    Stack* _stack;       // stack array
};

} // co
