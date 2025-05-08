#include "co/tasked.h"
#include "co/stl.h"
#include "co/time.h"
#include "co/thread.h"

namespace co {

struct tasked_impl {
    typedef std::function<void()> F;

    struct task {
        task(F&& f, int p, int c)
            : fun(std::move(f)), period(p), count(c) {
        }
        F fun;
        int period; // in seconds
        int count;
    };

    tasked_impl()
        : _stop(0), _ev(), _mtx() {
        co::thread(&tasked_impl::loop, this).detach();
        _tasks.reserve(32);
        _new_tasks.reserve(32);
    }

    ~tasked_impl() {
        this->stop();
    }

    void run_in(F&& f, int sec) {
        co::mutex_guard g(_mtx);
        _new_tasks.emplace_back(std::move(f), 0, sec);
        if (sec <= 0) _ev.signal();
    }

    void run_every(F&& f, int sec) {
        co::mutex_guard g(_mtx);
        _new_tasks.emplace_back(std::move(f), sec, sec);
    }

    void run_at(F&& f, int hour, int minute, int second, bool daily);

    void stop();

    void loop();

    int _stop;
    co::vector<task> _tasks;
    co::vector<task> _new_tasks;
    co::sync_event _ev;
    co::mutex _mtx;
};

// if @daily is false, run f() only once, otherwise run f() every day at hour:minute:second
void tasked_impl::run_at(F&& f, int hour, int minute, int second, bool daily) {
    assert(0 <= hour && hour <= 23);
    assert(0 <= minute && minute <= 59);
    assert(0 <= second && second <= 59);

    fastring t = time::str("%H%M%S");
    int now_hour = (t[0] - '0') * 10 + (t[1] - '0');
    int now_min  = (t[2] - '0') * 10 + (t[3] - '0');
    int now_sec  = (t[4] - '0') * 10 + (t[5] - '0');

    int now_seconds = now_hour * 3600 + now_min * 60 + now_sec;
    int seconds = hour * 3600 + minute * 60 + second;
    if (seconds < now_seconds) seconds += 86400;
    int diff = seconds - now_seconds;

    co::mutex_guard g(_mtx);
    _new_tasks.emplace_back(std::move(f), (daily ? 86400 : 0), diff);
}

void tasked_impl::loop() {
    int64 ms = 0;
    int sec = 0;
    time::timer timer;
    co::vector<task> tmp;

    while (!_stop) {
        timer.restart();
        {
            co::mutex_guard g(_mtx);
            if (!_new_tasks.empty()) _new_tasks.swap(tmp);
        }

        if (!tmp.empty()) {
            for (auto& x : tmp) _tasks.emplace_back(std::move(x));
            tmp.clear();
            if (tmp.capacity() >= 4096) co::vector<task>().swap(tmp);
        }

        if (ms >= 1000) {
            sec = (int) (ms / 1000);
            ms -= sec * 1000;
        }

        for (size_t i = 0; i < _tasks.size();) {
            auto& t = _tasks[i];
            if ((t.count -= sec) <= 0) {
                t.fun();
                if (t.period > 0) {
                    t.count = t.period;
                    ++i;
                } else {
                    if (i < _tasks.size() - 1) t = std::move(_tasks.back());
                    _tasks.pop_back();
                }
            } else {
                ++i;
            }
        }

        _ev.wait(1000);
        if (_stop) { atomic_store(&_stop, 2); return; }
        ms += timer.ms();
    }
}

void tasked_impl::stop() {
    int x = atomic_cas(&_stop, 0, 1);
    if (x == 0) {
        _ev.signal();
        while (_stop != 2) time::sleep(1);

        co::mutex_guard g(_mtx);
        _tasks.clear();
        _new_tasks.clear();
    } else {
        while (_stop != 2) time::sleep(1);
    }
}

tasked::tasked() {
    _p = new (co::alloc(sizeof(tasked_impl)))tasked_impl();
}

tasked::~tasked() {
    if (_p) {
        ((tasked_impl*)_p)->~tasked_impl();
        co::free(_p, sizeof(tasked_impl));
        _p = 0;
    }
}

void tasked::run_in(F&& f, int sec) {
    ((tasked_impl*)_p)->run_in(std::move(f), sec);
}

void tasked::run_every(F&& f, int sec) {
    ((tasked_impl*)_p)->run_every(std::move(f), sec);
}

void tasked::run_at(F&& f, int hour, int minute, int second) {
    ((tasked_impl*)_p)->run_at(std::move(f), hour, minute, second, false);
}

void tasked::run_daily(F&& f, int hour, int minute, int second) {
    ((tasked_impl*)_p)->run_at(std::move(f), hour, minute, second, true);
}

void tasked::stop() {
    ((tasked_impl*)_p)->stop();
}

} // co
