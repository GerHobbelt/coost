#include "sched.h"
#include "co/mem.h"
#include "co/os.h"
#include "co/rand.h"
#include "co/time.h"

DEF_mlstr(co_sched_num, "@h 协程调度器数量", "@h number of coroutine schedulers");
DEF_mlstr(co_stack_num, "@h 协程调度器的栈数量(必须是2的n次方)", "@h number of stacks per scheduler(must be power of 2)");
DEF_mlstr(co_stack_size, "@h 协程栈大小", "@h size of the coroutine stack");

DEF_uint32(co_sched_num, os::cpunum(), MLS_co_sched_num);
DEF_uint32(co_stack_num, 8, MLS_co_stack_num);
DEF_uint32(co_stack_size, 1024 * 1024, MLS_co_stack_size);

#ifdef _MSC_VER
extern LONG WINAPI _co_on_exception(PEXCEPTION_POINTERS p);
#endif

namespace co {

#ifdef _WIN32
extern void init_sock();
extern void cleanup_sock();
#else
inline void init_sock() {}
inline void cleanup_sock() {}
#endif

__thread Sched* g_sched;
co::vector<Sched*>* g_all_scheds;
uint32 g_sched_num;
uint32 g_stack_num = 8;
uint32 g_stack_size = 1 << 20;

Sched::Sched(uint32 id)
    : _epoll(std::bind(&Sched::resume, this, std::placeholders::_1)),
      _id(id), _seed(co::rand()), _stopped(false), _timeout(false),
      _wait_ms(-1), _running(0) {
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
}

void Sched::stop() {
    if (atomic_swap(&_stopped, true, mo_acq_rel) == false) {
        _epoll.signal();
        _ev.wait();
    }
}

void Sched::main_func(tb_context_from_t from) {
    ((Coroutine*)from.priv)->ctx = from.ctx;
  #ifdef _MSC_VER
    __try {
        ((Coroutine*)from.priv)->sched->running()->cb->run();
    } __except(_co_on_exception(GetExceptionInformation())) {
    }
  #else
    ((Coroutine*)from.priv)->sched->running()->cb->run();
  #endif // _WIN32
    tb_context_jump(from.ctx, 0); // jump back to the from context
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
void Sched::resume(void* c) {
    tb_context_from_t from;
    Coroutine* co = (Coroutine*)c;
    Stack* const s = co->sched->get_stack(co->id);
    _running = co;
    if (s->p == nullptr) {
        s->p = (char*) co::alloc(g_stack_size);
        s->top = s->p + g_stack_size;
        s->co = co;
    }

    if (co->ctx == nullptr) { /* resume new coroutine */
        if (s->co != co) { this->save_stack(s->co); s->co = co; }
        co->ctx = tb_context_make(s->p, g_stack_size, main_func);
        CO_DLOG("resume new co: ", co, " id: ", co->id);
        from = tb_context_jump(co->ctx, _main_co); // jump to main_func(from): from.priv == _main_co

    } else { /* resume suspended coroutine */
        if (co->it != _timer_mgr.end()) { /* remove the timer */
            CO_DLOG("del timer: ", co->it);
            _timer_mgr.del_timer(co->it);
            co->it = _timer_mgr.end();
        }

        CO_DLOG("resume co: ", co, " id: ", co->id, " stack: ", co->buf.size());
        if (s->co != co) {
            this->save_stack(s->co);
            if (s->top == (char*)co->ctx + co->buf.size()) {
                memcpy(co->ctx, co->buf.data(), co->buf.size()); // restore stack data
                s->co = co;
            } else {
                ::abort();
            }
        }
        from = tb_context_jump(co->ctx, _main_co); // jump back to where yiled was called
    }

    if (from.priv) { /* yield was called in coroutine, update the context */
        assert(_running == from.priv);
        _running->ctx = from.ctx;
        CO_DLOG("yield co: ", _running, " id: ", _running->id);
    } else { /* coroutine terminated */
        _running->sched->get_stack(_running->id)->co = nullptr;
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
        const int n = _epoll.wait(_wait_ms);
        if (_stopped) break;
        if (n < 0) continue;

        CO_DLOG("> check I/O tasks ready to resume, num: ", n);
        _epoll.handle_events();

        CO_DLOG("> check tasks ready to resume..");
        do {
            _task_mgr.get_all_tasks(_new_tasks, _ready_tasks);

            if (!_new_tasks.empty()) {
                CO_DLOG(">> resume new tasks, num: ", _new_tasks.size());
                for (size_t i = 0; i < _new_tasks.size(); ++i) {
                    this->resume(this->new_coroutine(_new_tasks[i]));
                }
                _clear_vector(_new_tasks, _new_tasks.size());
            }

            if (!_ready_tasks.empty()) {
                CO_DLOG(">> resume ready tasks, num: ", _ready_tasks.size());
                for (size_t i = 0; i < _ready_tasks.size(); ++i) {
                    this->resume(_ready_tasks[i]);
                }
                _clear_vector(_ready_tasks, _ready_tasks.size());
            }

            const size_t n = this->steal_tasks();
            if (n > 0) {
                CO_DLOG(">> resume stolen tasks, num: ", n);
                co::closure** const p = _new_tasks.data();
                for (size_t i = 0; i < n; ++i) {
                    this->resume(this->new_coroutine(p[i]));
                }
                _clear_vector(_new_tasks, n);
            }
        } while (0);

        CO_DLOG("> check timedout tasks..");
        do {
            _wait_ms = _timer_mgr.check_timeout(_ready_tasks);

            if (!_ready_tasks.empty()) {
                CO_DLOG(">> resume timedout tasks, num: ", _ready_tasks.size());
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
        if (co->it != _timer.end()) co->it = _timer.end();
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
