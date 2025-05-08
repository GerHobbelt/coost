#pragma once

#include "def.h"
#include <functional>

namespace co {

// timed task scheduler
struct tasked {
    typedef std::function<void()> F;

    tasked();
    ~tasked();

    tasked(const tasked&) = delete;
    void operator=(const tasked&) = delete;

    tasked(tasked&& t) : _p(t._p) {
        t._p = 0;
    }

    // run f() once @sec seconds later
    void run_in(F&& f, int sec);

    // run f() once @sec seconds later
    void run_in(const F& f, int sec) {
        this->run_in(F(f), sec);
    }

    // run f() every @sec seconds
    void run_every(F&& f, int sec);

    // run f() every @sec seconds
    void run_every(const F& f, int sec) {
        this->run_every(F(f), sec);
    }

    // run f() once at hour:minute:second
    // hour: 0-23, mimute & second: 0-59
    void run_at(F&& f, int hour, int minute=0, int second=0);

    // run f() once at hour:minute:second
    void run_at(const F& f, int hour, int minute=0, int second=0) {
        this->run_at(F(f), hour, minute, second);
    }

    // run f() at hour:minute:second every day
    // hour: 0-23, mimute & second: 0-59
    void run_daily(F&& f, int hour=0, int minute=0, int second=0);

    // run f() at hour:minute:second every day
    void run_daily(const F& f, int hour=0, int minute=0, int second=0) {
        this->run_daily(F(f), hour, minute, second);
    }

    // stop this task scheduler
    void stop();

    void* _p;
};

} // co
