#include "sched.h"
#include "co/mem.h"
#include "epoll/kqueue.h"

namespace co {

__thread Sched* g_sched;
co::vector<Sched*>* g_all_scheds;
uint32 g_sched_num;
uint32 g_stack_num = 8;
uint32 g_stack_size = 1 << 20;

Sched::Sched(uint32 id)
    : _id(id), _seed(co::rand()), _stopped(false), _timeout(false),
      _wait_ms(-1), _running(0) {
    _epoll = new (co::alloc(sizeof(Epoll), L1_CACHE_LINE_SIZE)) Epoll();
    _main_co = _co_pool.pop();
    _main_co->id = _idgen.pop(); // id 0 is reserved for _main_co
    _main_co->sched = this;
    _stack = (Stack*) co::zalloc(g_stack_num * sizeof(Stack));
}

Sched::~Sched() {
    this->stop();
    _idgen.push(_main_co->id);
    _co_pool.push(_main_co);
    co::free(_stack, g_stack_num * sizeof(Stack));
    _epoll->~Epoll();
    co::free(_epoll, sizeof(Epoll));
}

void Sched::stop() {
    if (atomic_swap(&_stopped, true, mo_acq_rel) == false) {
        _epoll->signal();
        _ev.wait();
    }
}

void Sched::main_func(tb_context_from_t from) {
    CO_DLOG("co main_func beg, from main ctx: ", from.ctx);
    ((Coroutine*)from.priv)->ctx = from.ctx; // update the main ctx
    ((Coroutine*)from.priv)->sched->running()->cb->run();
    CO_DLOG("co main_func end, back to main ctx: ", ((Coroutine*)from.priv)->ctx);
    tb_context_jump(((Coroutine*)from.priv)->ctx, 0); // DO NOT use from.ctx here
}

/*
 *  scheduling thread:
 *
 *    resume(co) -> jump(co->ctx, main_co)
 *       ^             |
 *       |             v
 *  jump(main_co)  main_func(from): from.priv == main_co
 *    yield()          |
 *       |             v
 *       <-------- co->cb->run():  run on _stack
 */
void Sched::resume(Coroutine* co) {
    Stack* const s = this->get_stack(co->id);
    tb_context_from_t from;
    _running = co;
    if (s->p == nullptr) {
        s->p = (char*) co::alloc(g_stack_size);
        s->top = s->p + g_stack_size;
        s->co = co;
    }

    if (co->ctx == nullptr) { /* resume new coroutine */
        if (s->co != co) {
            save_stack(s->co, s);
            s->co = co;
        }
        co->ctx = tb_context_make(s->p, g_stack_size, main_func);
        CO_DLOG(S, " resume new c", co->id, '(', co, "), ctx: ", co->ctx);
        from = tb_context_jump(co->ctx, _main_co); // jump to main_func(from): from.priv == _main_co

    } else { /* resume suspended coroutine */
        if (co->has_timer) { /* remove the timer */
            CO_DLOG(SC, " del timer: ", co->it);
            _timer_mgr.del_timer(co->it);
            co->has_timer = false;
        }

        if (s->co != co) {
            save_stack(s->co, s);
            restore_stack(co, s);
            s->co = co;
        }
        CO_DLOG(S, " resume c", co->id, ", ctx: ", co->ctx, " stack: ", co->buf.size());
        from = tb_context_jump(co->ctx, _main_co); // jump back to where yiled was called
    }

    if (from.priv) { /* yield was called in coroutine, update the context */
        assert(_running == from.priv);
        _running->ctx = from.ctx;
        CO_DLOG(S, " yield c", _running->id, ", ctx -> ", from.ctx);
    } else { /* coroutine terminated */
        s->co = nullptr;
        this->free_coroutine(_running);
    }
}

template<typename T>
inline void _clear_vector(co::vector<T>& v, size_t n) {
    const size_t c = v.capacity();
    if (c >= 16 * 1024 || (c >= 4096 && n <= (c >> 1))) {
        co::vector<T>().swap(v);
        v.reserve(512);
    }
    v.clear();
}

void Sched::loop() {
    g_sched = this;
    _new_tasks.reserve(512);
    _ready_tasks.reserve(512);

    while (!_stopped) {
        const int n = _epoll->wait(_wait_ms);
        if (_stopped) break;
        if (n < 0) continue;

        CO_DLOG(S, " > check I/O tasks ready to resume, num: ", n);
        _epoll->handle_events();

        CO_DLOG(S, " > check tasks ready to resume..");
        do {
            _task_mgr.get_all_tasks(_new_tasks, _ready_tasks);

            if (!_new_tasks.empty()) {
                CO_DLOG(S, " >> resume new tasks, num: ", _new_tasks.size());
                for (size_t i = 0; i < _new_tasks.size(); ++i) {
                    this->resume(this->new_coroutine(_new_tasks[i]));
                }
                _clear_vector(_new_tasks, _new_tasks.size());
            }

            if (!_ready_tasks.empty()) {
                CO_DLOG(S, " >> resume ready tasks, num: ", _ready_tasks.size());
                for (size_t i = 0; i < _ready_tasks.size(); ++i) {
                    this->resume(_ready_tasks[i]);
                }
                _clear_vector(_ready_tasks, _ready_tasks.size());
            }

            if (g_sched_num > 1) {
                const size_t n = this->steal_tasks();
                if (n > 0) {
                    CO_DLOG(S, " >> resume stolen tasks, num: ", n);
                    co::closure** const p = _new_tasks.data();
                    for (size_t i = 0; i < n; ++i) {
                        this->resume(this->new_coroutine(p[i]));
                    }
                    _clear_vector(_new_tasks, n);
                }
            }
        } while (0);

        CO_DLOG(S, " > check timedout tasks..");
        do {
            _wait_ms = _timer_mgr.check_timeout(_ready_tasks);

            if (!_ready_tasks.empty()) {
                CO_DLOG(S, " >> resume timedout tasks, num: ", _ready_tasks.size());
                _timeout = true;
                for (size_t i = 0; i < _ready_tasks.size(); ++i) {
                    this->resume(_ready_tasks[i]);
                }
                _timeout = false;
                _clear_vector(_ready_tasks, _ready_tasks.size());
            }
        } while (0);

        if (_running) _running = 0;
    }

    _ev.signal();
}

uint32 TimerManager::check_timeout(co::vector<Coroutine*>& res) {
    if (_timer.empty()) return (uint32)-1;

    int64 now_ms = time::mono.ms();
    auto it = _timer.begin();
    for (; it != _timer.end(); ++it) {
        if (it->first > now_ms) break;
        Coroutine* co = it->second;
        co->has_timer = false;
        if (!co->wtx) {
            res.push_back(co);
        } else {
            auto w = co->wtx;
            if (atomic_bool_cas(&w->state, st_wait, st_timeout, mo_relaxed, mo_relaxed)) {
                res.push_back(co);
            }
        }
    }

    if (it != _timer.begin()) {
        if (_it != _timer.end() && _it->first <= now_ms) _it = it;
        _timer.erase(_timer.begin(), it);
    }

    return _timer.empty() ? (uint32)-1 : (uint32)(_timer.begin()->first - now_ms);
}

} // co
