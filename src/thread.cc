#include "co/thread.h"
#include "./thread.h"
#include "co/mem.h"

namespace co {
namespace xx {

__thread uint32 g_tid;
uint32 thread_id() { return _thread_id(); }

} // xx

struct sync_event_impl {
    explicit sync_event_impl(bool m, bool s)
        : _wt(0), _sn(0), _signaled(s), _manual_reset(m) {
        _cv_init(&_cv);
    }

    ~sync_event_impl() {
        _cv_free(&_cv);
    }

    void wait() {
        _mutex_guard g(_m);
        if (_signaled) {
            if (!_manual_reset) _signaled = false;
            return;
        }
        ++_wt;
        _cv_wait(&_cv, _m.native_handle());
    }

    bool wait(uint32 ms) {
        _mutex_guard g(_m);
        if (_signaled) {
            if (!_manual_reset) _signaled = false;
            return true;
        }
        if (ms == 0) return false;

        const uint32 sn = _sn;
        ++_wt;
        const bool r = _cv_wait(&_cv, _m.native_handle(), ms);
        if (!r && sn == _sn) { assert(_wt > 0); --_wt; }
        return r;
    }

    void signal() {
        _mutex_guard g(_m);
        if (_wt > 0) {
            _wt = 0;
            if (_signaled && !_manual_reset) _signaled = false;
            ++_sn;
            _cv_notify_all(&_cv);
        } else {
            if (!_signaled ) _signaled = true;
        }
    }

    void reset() {
        _mutex_guard g(_m);
        _signaled = false;
    }

    _mutex _m;
    _cv_t _cv;
    uint32 _wt;
    uint32 _sn;
    bool _signaled;
    const bool _manual_reset;
};

sync_event::sync_event(bool manual_reset, bool signaled) {
    _p = co::alloc(sizeof(sync_event_impl), co::cache_line_size); assert(_p);
    new (_p) sync_event_impl(manual_reset, signaled);
}

sync_event::~sync_event() {
    if (_p) {
        ((sync_event_impl*)_p)->~sync_event_impl();
        co::free(_p, sizeof(sync_event_impl));
        _p = 0;
    }
}

void sync_event::signal() {
    ((sync_event_impl*)_p)->signal();
}

void sync_event::reset() {
    ((sync_event_impl*)_p)->reset();
}

void sync_event::wait() {
    ((sync_event_impl*)_p)->wait();
}

bool sync_event::wait(uint32 ms) {
    return ((sync_event_impl*)_p)->wait(ms);
}

} // co
