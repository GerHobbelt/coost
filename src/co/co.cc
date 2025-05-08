#include "co/co.h"
#include "co/atomic.h"
#include "co/flag.h"
#include "co/mem.h"
#include "co/clist.h"
#include "co/os.h"
#include "co/stl.h"
#include "sched.h"
#include "../thread.h"

namespace co {

struct Mod {
    Mod() : seed(co::rand()) {}
    ~Mod() = default;

    void add_task(co::closure* c) {
        !scheds.empty() ? next_sched()->add_new_task(c) : tasks.push_back(c);
    }

    void start_scheds();

    uint32 seed;
    co::vector<Sched*> scheds;
    co::vector<co::closure*> tasks;
    std::function<Sched*()> next_sched;
};

static Mod* g_mod;

void go(co::closure* c) {
    g_mod->add_task(c);
}

int sched_num() { return g_sched_num; }

sched_t* sched() { return (sched_t*) g_sched; }

coro_t* coroutine() {
    const auto s = g_sched;
    return s ? (coro_t*)s->running() : 0;
}

int sched_id() {
    const auto s = g_sched;
    return s ? s->id() : -1;
}

int coroutine_id() {
    const auto s = g_sched;
    return (s && s->running()) ? s->coroutine_id() : -1;
}

void yield() {
    const auto s = g_sched;
    s->yield();
}

void resume(coro_t* p) {
    const auto co = (Coroutine*)p;
    co->sched->add_ready_task(co);
}

void sleep(uint32 ms) {
    const auto s = g_sched;
    s ? s->sleep(ms) : time::sleep(ms);
}

bool timeout() {
    const auto s = g_sched;
    return s->timeout();
}

bool on_stack(const void* p) {
    const auto s = g_sched;
    return s->on_stack(p);
}

void add_timer(uint32 ms) {
    const auto s = g_sched;
    s->add_timer(ms);
}

void add_io_event(sock_t fd, ev_t ev) {
    const auto s = g_sched;
    s->add_io_event(fd, ev);
}

void del_io_event(sock_t fd, ev_t ev) {
    const auto s = g_sched;
    s->del_io_event(fd, ev);
}

void del_io_event(sock_t fd) {
    const auto s = g_sched;
    s->del_io_event(fd);
}


struct cutex_impl {
    // for non-coroutine waiters
    struct ncw_t {
        _cv_t cv;
        void* co;
    };

    cutex_impl()
        : _m(), _ncw(nullptr), _wq(), _refn(1), _lock(0) {
    }

    ~cutex_impl() {
        if (_ncw) {
            _cv_free(&_ncw->cv);
            co::free(_ncw, sizeof(ncw_t));
            _ncw = nullptr;
        }
    }

    void lock();
    void unlock();
    bool try_lock();

    void ref() { atomic_inc(&_refn, mo_relaxed); }
    uint32 unref() { return atomic_dec(&_refn, mo_acq_rel); }

    _mutex _m;
    ncw_t* _ncw;
    co::clist _wq;
    uint32 _refn;
    uint32 _lock;
};

inline bool cutex_impl::try_lock() {
    _mutex_guard g(_m);
    return _lock != 0 ? false : (_lock = 1);
}

void cutex_impl::lock() {
    const auto s = g_sched;
    if (s) { /* in coroutine */
        _m.lock();
        if (_lock == 0) {
            _lock = 1;
            _m.unlock();
        } else {
            _wq.push_back((clink*)s->running());
            _m.unlock();
            s->yield();
        }

    } else { /* non-coroutine */
        _mutex_guard g(_m);
        if (_lock == 0) {
            _lock = 1;
        } else {
            static_assert(alignof(Coroutine) == alignof(void*), "");
            void* buf[sizeof(Coroutine) / sizeof(void*)];
            Coroutine* co = (Coroutine*) buf;
            co->sched = nullptr;
            _wq.push_back((clink*)co);

            if (!_ncw) {
                _ncw = (ncw_t*) co::alloc(sizeof(ncw_t), co::cache_line_size);
                _cv_init(&_ncw->cv);
            }

            for (;;) {
                _cv_wait(&_ncw->cv, _m.native_handle());
                if (_lock == 2) {
                    if (_ncw->co != co) {
                        _wq.push_front((clink*)_ncw->co);
                        _wq.erase((clink*)co);
                    }
                    _lock = 1;
                    break;
                }
            }
        }
    }
}

void cutex_impl::unlock() {
    _m.lock();
    if (_wq.empty()) {
        _lock = 0;
        _m.unlock();
    } else {
        Coroutine* const co = (Coroutine*) _wq.pop_front();
        if (co->sched) {
            _m.unlock();
            co->sched->add_ready_task(co);
        } else {
            _lock = 2;
            _ncw->co = co;
            _m.unlock();
            _cv_notify_one(&_ncw->cv);
        }
    }
}

cutex::cutex() {
    _p = co::alloc(sizeof(cutex_impl), co::cache_line_size);
    new (_p) cutex_impl();
}

cutex::cutex(const cutex& m) : _p(m._p) {
    if (_p) god::cast<cutex_impl*>(_p)->ref();
}

cutex::~cutex() {
    const auto p = (cutex_impl*)_p;
    if (p && p->unref() == 0) {
        p->~cutex_impl();
        co::free(_p, sizeof(cutex_impl));
        _p = 0;
    }
}

void cutex::lock() const {
    god::cast<cutex_impl*>(_p)->lock();
}

void cutex::unlock() const {
    god::cast<cutex_impl*>(_p)->unlock();
}

bool cutex::try_lock() const {
    return god::cast<cutex_impl*>(_p)->try_lock();
}


struct event_impl {
    struct ncw_t {
        _cv_t cv;
        uint32 wt;
        uint32 sn;
    };

    event_impl(bool m, bool s)
        : _m(), _ncw(nullptr), _wc(), _refn(1), _signaled(s), _manual_reset(m) {
    }

    ~event_impl() {
        if (_ncw) {
            _cv_free(&_ncw->cv);
            co::free(_ncw, sizeof(ncw_t));
            _ncw = nullptr;
        }
    }

    bool wait(uint32 ms);
    void signal();
    void reset();

    void ref() { atomic_inc(&_refn, mo_relaxed); }
    uint32 unref() { return atomic_dec(&_refn, mo_acq_rel); }

    _mutex _m;
    ncw_t* _ncw;
    co::clist _wc;
    uint32 _refn;
    bool _signaled;
    const bool _manual_reset;
};

bool event_impl::wait(uint32 ms) {
    const auto s = g_sched;
    Coroutine* co = s ? s->running() : nullptr;

    if (co) { /* in coroutine */
        _m.lock();
        if (_signaled) {
            if (!_manual_reset) _signaled = false;
            _m.unlock();
            return true;
        }
        if (ms == 0) {
            _m.unlock();
            return false;
        }

        if (ms != (uint32)-1) {
            Waitx* w = (Waitx*) _wc.front();
            if (w && w->co && w->state == st_timeout) {
                _wc.pop_front();
                w->co = co;
                w->state = st_wait;

                // signal() may be never called, free timedout Waitx here
                while (!_wc.empty()) {
                    const auto x = (Waitx*) _wc.front();
                    if (!x->co || x->state != st_timeout) break;
                    _wc.pop_front();
                    co::free(x, sizeof(*x));
                }

            } else {
                w = Waitx::create(co);
            }

            co->wtx = w;
            _wc.push_back((clink*)w);
            _m.unlock();

            s->add_timer(ms);
            s->yield();
            if (!s->timeout()) co::free(co->wtx, sizeof(Waitx));
            co->wtx = nullptr;

        } else {
            static_assert(offsetof(Coroutine, wtx) == offsetof(Waitx, co));
            auto w = (Waitx*) co;
            w->co = 0; // co->wtx = 0
            _wc.push_back((clink*)w);
            _m.unlock();
            s->yield();
        }

        return !s->timeout();
       
    } else { /* non-coroutine */
        _mutex_guard g(_m);
        if (_signaled) {
            if (!_manual_reset) _signaled = false;
            return true;
        }
        if (ms == 0) return false;

        if (!_ncw) {
            _ncw = (ncw_t*) co::alloc(sizeof(ncw_t), co::cache_line_size);
            _cv_init(&_ncw->cv);
            _ncw->wt = 0;
            _ncw->sn = 0;
        }

        const uint32 sn = _ncw->sn;
        ++_ncw->wt;
        if (ms != (uint32)-1) {
            const bool r = _cv_wait(&_ncw->cv, _m.native_handle(), ms);
            if (!r && sn == _ncw->sn) { --_ncw->wt; }
            return r;
        } else {
            _cv_wait(&_ncw->cv, _m.native_handle());
            return true;
        }
    }
}

void event_impl::signal() {
    co::clink* h = 0;
    {
        bool has_wc = false;
        _mutex_guard g(_m);
         const bool has_wt = _ncw && _ncw->wt > 0;

        if (!_wc.empty()) {
            h = _wc.front();
            _wc.clear();
            if (!has_wt) {
                do {
                    const auto w = (Waitx*)h;
                    h = h->next;
                    if (!w->co) {
                        has_wc = true;
                        ((Coroutine*)w)->sched->add_ready_task((Coroutine*)w);
                        break;
                    }

                    if (atomic_bool_cas(&w->state, st_wait, st_ready, mo_relaxed, mo_relaxed)) {
                        has_wc = true;
                        w->co->sched->add_ready_task(w->co);
                        break;
                    } else { /* timeout */
                        co::free(w, sizeof(*w));
                    }
                } while (h);
            }
        }

        if (has_wc || has_wt) {
            if (_signaled && !_manual_reset) _signaled = false;
            if (has_wt) {
                _ncw->wt = 0;
                ++_ncw->sn;
                _cv_notify_all(&_ncw->cv);
            }
        } else {
            if (!_signaled) _signaled = true;
        }
    }

    while (h) {
        const auto w = (Waitx*)h;
        h = h->next;
        if (!w->co) {
            ((Coroutine*)w)->sched->add_ready_task((Coroutine*)w);
        } else {
            if (atomic_bool_cas(&w->state, st_wait, st_ready, mo_relaxed, mo_relaxed)) {
                w->co->sched->add_ready_task(w->co);
            } else { /* timeout */
                co::free(w, sizeof(*w));
            }
        }
    }
}

inline void event_impl::reset() {
    _mutex_guard g(_m);
    _signaled = false;
}

event::event(bool manual_reset, bool signaled) {
    _p = co::alloc(sizeof(event_impl), co::cache_line_size);
    new (_p) event_impl(manual_reset, signaled);
}

event::event(const event& e) : _p(e._p) {
    if (_p) god::cast<event_impl*>(_p)->ref();
}

event::~event() {
    const auto p = (event_impl*)_p;
    if (p && p->unref() == 0) {
        p->~event_impl();
        co::free(_p, sizeof(event_impl));
        _p = 0;
    }
}

bool event::wait(uint32 ms) const {
    return god::cast<event_impl*>(_p)->wait(ms);
}

void event::signal() const {
    god::cast<event_impl*>(_p)->signal();
}

void event::reset() const {
    god::cast<event_impl*>(_p)->reset();
}


struct wait_group_impl {
    wait_group_impl(uint32 n)
        : _m(), _co(nullptr), _refn(1), _n(n) {
        _cv_init(&_cv);
    }

    ~wait_group_impl() {
        _cv_free(&_cv);
    }

    void add(uint32 n);
    void done();
    void wait();

    void ref() { atomic_inc(&_refn, mo_relaxed); }
    uint32 unref() { return atomic_dec(&_refn, mo_acq_rel); }

    _mutex _m;
    _cv_t _cv;
    void* _co;
    uint32 _refn;
    uint32 _n;

};

inline void wait_group_impl::add(uint32 n) {
    atomic_add(&_n, n, mo_relaxed);
}

void wait_group_impl::done() {
    const uint32 n = atomic_dec(&_n, mo_acq_rel);
    if (n == (uint32)-1) ::abort();
    if (n == 0) {
        _m.lock();
        const auto co = (Coroutine*) _co;
        if (co) {
            _co = nullptr;
            _m.unlock();

            if (co->sched) {
                co->sched->add_ready_task(co);
            } else {
                _cv_notify_one(&_cv);
            }

        } else {
            _co = (void*)1;
            _m.unlock();
        }
    }
}

void wait_group_impl::wait() {
    const auto s = g_sched;
    _m.lock();

    if (_co == nullptr) {
        if (s) {
            _co = s->running();
            _m.unlock();
            s->yield();
        } else {
            static_assert(alignof(Coroutine) == alignof(void*), "");
            void* buf[sizeof(Coroutine) / sizeof(void*)];
            ((Coroutine*)buf)->sched = nullptr;
            _co = buf;
            _cv_wait(&_cv, _m.native_handle());
            _m.unlock();
        }

    } else {
        if (_co == (void*)1) { /* already signaled */
            _co = nullptr;
            _m.unlock();
        } else {
            ::abort();
        }
    }
}

wait_group::wait_group(uint32 n) {
    _p = co::alloc(sizeof(wait_group_impl), co::cache_line_size);
    new (_p) wait_group_impl(n);
}

wait_group::wait_group(const wait_group& wg) : _p(wg._p) {
    if (_p) god::cast<wait_group_impl*>(_p)->ref();
}

wait_group::~wait_group() {
    const auto p = (wait_group_impl*)_p;
    if (p && p->unref() == 0) {
        p->~wait_group_impl();
        co::free(_p, sizeof(wait_group_impl));
        _p = 0;
    }
}

void wait_group::add(uint32 n) const {
    god::cast<wait_group_impl*>(_p)->add(n);
}

void wait_group::done() const {
    god::cast<wait_group_impl*>(_p)->done();
}

void wait_group::wait() const {
    god::cast<wait_group_impl*>(_p)->wait();
}


struct pool_impl {
    typedef co::vector<void*> V;
    typedef std::function<void*()> create_cb_t;
    typedef std::function<void(void*)> destroy_cb_t;

    pool_impl()
        : _cap((uint32)-1), _refn(1) {
        _pools.resize(g_sched_num);
    }

    pool_impl(create_cb_t&& c, destroy_cb_t&& d, uint32 cap)
        : _cap(cap), _refn(1), _c(std::move(c)), _d(std::move(d)) {
        _pools.resize(g_sched_num);
    }

    ~pool_impl() {
        this->clear();
    }

    void* pop();
    void push(void* p);
    void clear();

    void ref() { atomic_inc(&_refn, mo_relaxed); }
    uint32 unref() { return atomic_dec(&_refn, mo_acq_rel); }

    co::vector<V> _pools;
    uint32 _cap;
    uint32 _refn;
    create_cb_t _c;
    destroy_cb_t _d;
};

inline void* pool_impl::pop() {
    auto s = g_sched;
    if (s) {
        auto& v = _pools[s->id()];
        if (!v.empty()) {
            const auto p = v.back();
            v.pop_back();
            return p;
        }
        return _c ? _c() : nullptr;
    }
    ::abort();
}

inline void pool_impl::push(void* p) {
    auto s = g_sched;
    if (s) {
        if (p) {
            auto& v = _pools[s->id()];
            ((uint32)v.size() < _cap || !_d) ? v.push_back(p) : _d(p);
        }
    } else {
        ::abort();
    }
}

void pool_impl::clear() {
    for (auto& v : _pools) {
        if (_d) {
            for (auto& x : v) _d(x);
        }
        V().swap(v);
    }
}

pool::pool() {
    _p = co::alloc(sizeof(pool_impl), co::cache_line_size);
    new (_p) pool_impl();
}

pool::pool(const pool& p) : _p(p._p) {
    if (_p) god::cast<pool_impl*>(_p)->ref();
}

pool::~pool() {
    const auto p = (pool_impl*)_p;
    if (p && p->unref() == 0) {
        p->~pool_impl();
        co::free(_p, sizeof(pool_impl));
        _p = 0;
    }
}

pool::pool(create_cb_t&& c, destroy_cb_t&& d, uint32 cap) {
    _p = co::alloc(sizeof(pool_impl), co::cache_line_size);
    new (_p) pool_impl(std::move(c), std::move(d), cap);
}

void* pool::pop() const {
    return god::cast<pool_impl*>(_p)->pop();
}

void pool::push(void* p) const {
    god::cast<pool_impl*>(_p)->push(p);
}

void Mod::start_scheds() {
    auto& n = g_sched_num;
    if (n != 1) {
        if ((n & (n - 1)) == 0) {
            next_sched = [this]() {
                const uint32 i = co::rand(this->seed) & (n - 1);
                return this->scheds[i];
            };
        } else {
            next_sched = [this]() {
                const uint32 i = co::rand(this->seed) % n;
                return this->scheds[i];
            };
        }
    } else {
        next_sched = [this]() {
            return this->scheds[0];
        };
    }

    scheds.reserve(n);
    for (uint32 i = 0; i < n; ++i) {
        Sched* s = co::_make_static<Sched>(i);
        s->start();
        scheds.push_back(s);
    }

    if (!tasks.empty()) {
        for (auto& c : tasks) this->next_sched()->add_new_task(c);
        co::vector<co::closure*>().swap(tasks);
    }
}

namespace xx {

static void unhide_flags() {
    flag::set_attr("co_sched_num", flag::attr_default);
    flag::set_attr("co_stack_num", flag::attr_default);
    flag::set_attr("co_stack_size", flag::attr_default);
}

static void init() {
    const uint32 ncpu = os::cpunum();
    auto& n = FLG_co_sched_num;
    auto& m = FLG_co_stack_num;
    auto& s = FLG_co_stack_size;
    if (n <= 0 || n > ncpu) n = ncpu;
    if (m <= 0 || (m & (m - 1)) != 0) m = 8;
    if (s <= 0) s = 1024 * 1024;
    g_sched_num = n;
    g_stack_num = m;
    g_stack_size = s;
    g_mod->start_scheds();
}

static int g_nifty_counter;

CoInit::CoInit() {
    const int n = ++g_nifty_counter;
    if (n == 1) {
        g_sched_num = os::cpunum();
        g_mod = co::_make_static<Mod>();
        g_all_scheds = &g_mod->scheds;
    }
    if (n == 2) {
        flag::run_before_parse(unhide_flags);
        flag::run_after_parse(init);
    }
}

CoInit::~CoInit() {
}

} // xx
} // co
