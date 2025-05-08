#pragma once

#include "def.h"
#include "mem.h"

#include <functional>
#include <type_traits>

namespace co {

struct closure {
    closure() = default;
    virtual ~closure() = default;
    
    virtual void run() = 0;

    template<typename F>
    static inline closure* create(F* f);

    template<typename F, typename P>
    static inline closure* create(F* f, P&& p);

    template<typename F>
    static inline closure* create(F&& f);

    template<typename F, typename P>
    static inline closure* create(F&& f, P&& p);

    template<typename T>
    static inline closure* create(void (T::*f)(), T* o);

    template<typename F, typename T, typename P>
    static inline closure* create(F&& f, T* o, P&& p);
};

namespace xx {

template<typename F>
struct func0 : closure {
    func0(F* f) : _f(f) {}
    virtual ~func0() = default;

    virtual void run() {
        (*_f)();
        co::_delete(this);
    }

    typename std::remove_reference<F>::type* _f;
};

template<typename F, typename P>
struct func1 : closure {
    func1(F* f, P&& p) : _f(f), _p(std::forward<P>(p)) {}
    virtual ~func1() = default;

    virtual void run() {
        (*_f)(_p);
        co::_delete(this);
    }

    typename std::remove_reference<F>::type* _f;
    typename std::remove_reference<P>::type _p;
};

template<typename F>
struct runnable0 : closure {
    runnable0(F&& f) : _f(std::forward<F>(f)) {}
    virtual ~runnable0() = default;

    virtual void run() {
        _f();
        co::_delete(this);
    }

    typename std::remove_reference<F>::type _f;
};

template<typename F, typename P>
struct runnable1: closure {
    runnable1(F&& f, P&& p) : _f(std::forward<F>(f)), _p(std::forward<P>(p)) {}
    virtual ~runnable1() = default;

    virtual void run() {
        _f(_p);
        co::_delete(this);
    }

    typename std::remove_reference<F>::type _f;
    typename std::remove_reference<P>::type _p;
};

template<typename T>
struct method0 : closure {
    typedef void (T::*F)();

    method0(F f, T* o) : _f(f), _o(o) {}
    virtual ~method0() = default;

    virtual void run() {
        (_o->*_f)();
        co::_delete(this);
    }

    F _f;
    T* _o;
};

template<typename F, typename T, typename P>
struct method1 : closure {
    method1(F&& f, T* o, P&& p)
        : _f(std::forward<F>(f)), _o(o), _p(std::forward<P>(p)) {
    }
    
    virtual ~method1() = default;

    virtual void run() {
        (_o->*_f)(_p);
        co::_delete(this);
    }

    typename std::remove_reference<F>::type _f;
    T* _o;
    typename std::remove_reference<P>::type _p;
};

} // xx

template<typename F>
inline closure* closure::create(F* f) {
    return co::_new<xx::func0<F>>(f);
}

template<typename F, typename P>
inline closure* closure::create(F* f, P&& p) {
    return co::_new<xx::func1<F, P>>(f, std::forward<P>(p));
}

template<typename F>
inline closure* closure::create(F&& f) {
    return co::_new<xx::runnable0<F>>(std::forward<F>(f));
}
template<typename F, typename P>
inline closure* closure::create(F&& f, P&& p) {
    return co::_new<xx::runnable1<F, P>>(std::forward<F>(f), std::forward<P>(p));
}

template<typename T>
inline closure* closure::create(void (T::*f)(), T* o) {
    return co::_new<xx::method0<T>>(f, o);
}

template<typename F, typename T, typename P>
inline closure* closure::create(F&& f, T* o, P&& p) {
    return co::_new<xx::method1<F, T, P>>(std::forward<F>(f), o, std::forward<P>(p));
}

} // co
