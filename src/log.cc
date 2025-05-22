#include "co/log.h"
#include "co/def.h"
#include "co/flag.h"
#include "co/fs.h"
#include "co/os.h"
#include "co/str.h"
#include "co/time.h"
#include "co/thread.h"
#include <time.h>
#ifdef WITH_BACKTRACE
#include <backtrace.h>
#include <cxxabi.h>
#endif

#ifdef _MSC_VER
#pragma warning (disable:4722) // call exit() in destructor
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

inline void _cerr(const void* s, size_t n) {
    auto r = ::fwrite(s, 1, n, stderr); (void)r;
}

inline void _sleep(int ms) { time::sleep(ms); }

#else
#include <unistd.h>
#include <sys/select.h>

inline void _cerr(const void* s, size_t n) {
    auto r = ::write(STDERR_FILENO, s, n); (void)r;
}

inline void _sleep(int ms) {
    struct timeval tv = { 0, ms * 1000 };
    ::select(0, 0, 0, 0, &tv);
}
#endif

inline void _cerr(const char* s) { _cerr(s, strlen(s)); }

DEF_mls(log_dir, "@i 日志目录", "@i log directory");
DEF_mls(max_log_size, "@i 单条日志最大大小", "@i max size of a single log");
DEF_mls(max_log_file_size, "@i 日志文件最大大小", "@i max size of log file");
DEF_mls(max_log_file_num, "@i 日志文件最大数量", "@i max number of log files");
DEF_mls(max_log_buffer_size, "@i 日志缓存最大大小", "@i max size of log buffer");
DEF_mls(log_flush_ms, "@i 刷新日志缓存时间间隔(单位毫秒)", "@i flush the log buffer every n ms");
DEF_mls(also_log2console, "@i 日志也输出到终端", "@i also logging to console");
DEF_mls(log_daily, "@i 日志文件按天轮转", "@i rotate log files by day");
DEF_mls(open_failed, "打开日志文件失败", "open log file failed");

static fastring* g_log_dir;
void _init_log_dir() {
    DEF_string(log_dir, "logs", MLS_log_dir);
    g_log_dir = &FLG_log_dir;
}
DEF_uint32(min_log_level, 0, "@i 0-4 (debug|info|warn|error|fatal)");
DEF_uint32(max_log_size, 4096, MLS_max_log_size);
DEF_int64(max_log_file_size, 256 << 20, MLS_max_log_file_size);
DEF_uint32(max_log_file_num, 8, MLS_max_log_file_num);
DEF_uint32(max_log_buffer_size, 32 << 20, MLS_max_log_buffer_size);
DEF_uint32(log_flush_ms, 128, MLS_log_flush_ms);
DEF_bool(also_log2console, false, MLS_also_log2console);
DEF_bool(log_daily, false, MLS_log_daily);

static void (*g_write_cb)(void*, size_t) = nullptr;
static bool g_also_log2local = false;
static bool g_has_fatal_log = false;
static bool g_has_exception = false;
static bool g_day_changed = false;
static uint32 g_day;         // current day
static uint32 g_last_day;    // last day
static char g_last_time[24]; // time before day changed

namespace _xx {
namespace log {
namespace xx {

struct LogTime;
struct LogFile;
struct Logger;
struct ExceptHandler;

struct Mod {
    Mod();
    ~Mod() = default;
    fastring* exename;
    fastring* stream;
    LogTime* log_time;
    LogFile* log_file;
    LogFile* log_fatal;
    Logger* logger;
    ExceptHandler* except_handler;
    bool padding;
};

static Mod* g_mod;
inline Mod& mod() { return *g_mod; }

// time for logs: "0723 17:00:00.123"
struct LogTime {
    enum {
        t_len = 17, // length of time 
        t_min = 8,  // position of minute
        t_sec = t_min + 3,
        t_ms = t_sec + 3,
    };

    LogTime() : _start(0) {
        for (int i = 0; i < 60; ++i) {
            const auto p = (uint8*) &_tb[i];
            p[0] = (uint8)('0' + i / 10);
            p[1] = (uint8)('0' + i % 10);
        }
        memset(_buf, 0, sizeof(_buf));
        this->update();
    }

    void update();
    const char* get() const { return _buf; }
    uint32 day() const { return *(uint32*)_buf; }

    time_t _start;
    struct tm _tm;
    int16 _tb[64];
    char _buf[24]; // save the time string
};

void LogTime::update() {
    const int64 now_ms = time::unix.ms();
    const time_t now_sec = now_ms / 1000;
    const int dt = (int) (now_sec - _start);
    if (dt == 0) goto set_ms;
    if (dt < 0 || dt >= 60 || _start == 0) goto reset;

    _tm.tm_sec += dt;
    if (_tm.tm_min < 59 || _tm.tm_sec < 60) {
        _start = now_sec;
        if (_tm.tm_sec >= 60) {
            _tm.tm_min++;
            _tm.tm_sec -= 60;
            *(uint16*)(_buf + t_min) = _tb[_tm.tm_min];
        }
        const auto p = (char*)(_tb + _tm.tm_sec);
        _buf[t_sec] = p[0];
        _buf[t_sec + 1] = p[1];
        goto set_ms;
    }

  reset:
    {
        _start = now_sec;
      #ifdef _WIN32
        _localtime64_s(&_tm, &_start);
      #else
        localtime_r(&_start, &_tm);
      #endif
        strftime(_buf, 16, "%m%d %H:%M:%S.", &_tm);
    }

  set_ms:
    {
        const auto p = _buf + t_ms;
        uint32 ms = (uint32)(now_ms - _start * 1000);
        uint32 x = ms / 100;
        p[0] = (char)('0' + x);
        ms -= x * 100;
        x = ms / 10;
        p[1] = (char)('0' + x);
        p[2] = (char)('0' + (ms - x * 10));
    }
}

struct LogFile {
    explicit LogFile(bool fatal)
        : _file(256), _path(256), _path_base(256),
          _fatal(fatal), _initialized(false) {
    }

    void init();
    fs::file& open();
    void write(void* p, size_t n);
    void close() { _file.close(); }
    void rotate();

    fs::file _file;
    fastring _path;
    fastring _path_base; // prefix of the log path: log_dir/log_file_name 
    co::deque<fastring> _old_paths; // paths of old log files
    bool _fatal;
    bool _initialized;
};

void LogFile::init() {
    auto& m = mod();
    auto& s = *m.stream;
    auto& d = *g_log_dir;
    for (size_t i = 0; i < d.size(); ++i) if (d[i] == '\\') d[i] = '/';

    // use process name as the log file name
    s.clear();
    s.append(*m.exename).remove_suffix(".exe");

    // prefix of log file path
    _path_base.cat(d);
    if (!_path_base.empty() && _path_base.back() != '/') _path_base.cat('/');
    _path_base.cat(s);

    // read paths of old log files
    if (!_fatal && !g_has_fatal_log && !g_has_exception) {
        s.clear();
        s.append(_path_base).append(".log.list");
        fs::file f(s.c_str(), 'r');
        if (f) {
            auto v = str::split(f.read(f.size()), '\n');
            for (auto& x : v) _old_paths.push_back(std::move(x));
        }
    }
}

// get day from xx_0808_15_30_08.123.log
inline uint32 get_day_from_path(const fastring& path) {
    uint32 x = 0;
    const int n = LogTime::t_len + 4;
    if (path.size() > n) god::copy<4>(&x, path.data() + path.size() - n);
    return x;
}

inline void LogFile::rotate() {
    _file.close();
    if (!_old_paths.empty()) {
        auto& path = _old_paths.back();
        fs::mv(_path, path); // rename xx.log to xx_0808_15_30_08.123.log
    }
}

fs::file& LogFile::open() {
    if (!_initialized) { this->init(); _initialized = true; }

    auto& m = mod();
    auto& s = *m.stream; s.clear();
    auto& d = *g_log_dir; (void)d.c_str();
    if (!fs::exists(d)) fs::mkdir(d.data(), true);

    _path.clear();
    if (!_fatal) {
        _path.append(_path_base).append(".log");

        bool new_file = !fs::exists(_path);
        if (!new_file) {
            if (!_old_paths.empty()) {
                if (FLG_log_daily) {
                    auto& path = _old_paths.back();
                    const uint32 day = !g_day_changed ? g_day : g_last_day;
                    if (get_day_from_path(path) != day) {
                        fs::mv(_path, path);
                        new_file = true;
                    }
                }
            } else {
                new_file = true;
            }
        }
      
        if (_file.open(_path.c_str(), 'a') && new_file) {
            char x[24] = { 0 }; // 0723 17:00:00.123
            const char* const t = !g_day_changed ? m.log_time->get() : g_last_time ;
            memcpy(x, t, LogTime::t_len);
            for (int i = 0; i < LogTime::t_len; ++i) {
                if (x[i] == ' ' || x[i] == ':') x[i] = '_';
            }

            s.clear();
            s.cat(_path_base, '_', x, ".log");
            _old_paths.push_back(s);

            while (!_old_paths.empty() && _old_paths.size() > FLG_max_log_file_num) {
                fs::rm(_old_paths.front());
                _old_paths.pop_front();
            }

            s.resize(_path_base.size());
            s.append(".log.list");
            fs::file f(s.c_str(), 'w');
            if (f) {
                s.clear();
                for (auto& x : _old_paths) s.cat(x, '\n');
                f.write(s);
            }
        }

    } else {
        _path.append(_path_base).append(".fatal");
        _file.open(_path.c_str(), 'a');
    }

    if (!_file) {
        s.clear();
        s.cat(MLS_open_failed, ": ", _path, '\n');
        _cerr(s.data(), s.size());
    }
    return _file;
}

void LogFile::write(void* p, size_t n) {
    if (_file || this->open()) {
        _file.write(p, n);
        const int64 x = _file.size(); // -1 if not exists
        if (x < 0) _file.close();
        if (!_fatal && (x >= FLG_max_log_file_size || g_day_changed)) {
            this->rotate();
        }
    }
}

struct Logger {
    static const uint32 N = 128 * 1024;

    Logger(LogTime* t, LogFile* f);
    ~Logger() { this->stop(); }

    void start();
    void stop(bool signal_safe=false);
    void push_normal_log(char* s, size_t n);
    void push_fatal_log(char* s, size_t n);

    struct alignas(64) X {
        std::mutex mtx;
        fastream buf;
        char time_str[24];
    };

    void write_logs(void* p, size_t n);
    void thread_fun();

    X _x;
    fastream _buf; // to swap out logs
    co::sync_event _log_event;
    LogTime& _time;
    LogFile& _file;
    uint32 _max_log_size;
    uint32 _max_log_buffer_size;
    int _stop; // -1: init, 0: running, 1: stopping, 2: stopped, 3: final
};

Logger::Logger(LogTime* t, LogFile* f)
    : _log_event(true, false), _time(*t), _file(*f), _stop(-1) {
    _max_log_size = 4096;
    _max_log_buffer_size = 32 << 20;
    memcpy(_x.time_str, _time.get(), 24);
}

void Logger::start() {
    _max_log_size = FLG_max_log_size;
    _max_log_buffer_size = FLG_max_log_buffer_size;

    _time.update();
    g_day = _time.day();
    memcpy(_x.time_str, _time.get(), 24);
    _x.buf.reserve(N);
    _buf.reserve(N);

    co::atomic_store(&_stop, 0);
    co::thread(&Logger::thread_fun, this).detach();
}

// if @signal_safe is true, try to call only async-signal-safe APIs according to:
// http://man7.org/linux/man-pages/man7/signal-safety.7.html
void Logger::stop(bool signal_safe) {
    int s = co::atomic_cas(&_stop, 0, 1);
    if (s < 0) return; // thread not started
    if (s == 0) {
        if (!signal_safe) _log_event.signal();
        while (_stop != 2) _sleep(1);

        do {
            // it may be not safe if logs are still being pushed to the buffer 
            !signal_safe ? _x.mtx.lock() : _sleep(1);
            if (!_x.buf.empty()) {
                _x.buf.swap(_buf);
                this->write_logs(_buf.data(), _buf.size());
                _buf.clear();
            }
            if (!signal_safe) _x.mtx.unlock();
        } while (0);

        co::atomic_swap(&_stop, 3);
    } else {
        while (_stop != 3) _sleep(1);
    }
}

void Logger::push_normal_log(char* s, size_t n) {
    if (n <= _max_log_size) goto _1;
    {
        n = _max_log_size;
        char* const p = s + n - 4;
        p[0] = '.';
        p[1] = '.';
        p[2] = '.';
        p[3] = '\n';
    }

_1:
    std::lock_guard<std::mutex> g(_x.mtx);
    auto& buf = _x.buf;

    if (_stop <= 0) {
        memcpy(s + 1, _x.time_str, LogTime::t_len); // log time

        if (buf.size() + n < _max_log_buffer_size) goto _2;
        {
            const char* p = strchr(buf.data() + (buf.size() >> 1) + 7, '\n');
            const size_t len = buf.data() + buf.size() - p - 1;
            memcpy((char*)(buf.data()), "......\n", 7);
            memcpy((char*)(buf.data()) + 7, p + 1, len);
            buf.resize(len + 7);
        }

    _2:
        buf.append(s, n);
        if (buf.size() > (buf.capacity() >> 1)) _log_event.signal();
    }
}

void Logger::push_fatal_log(char* s, size_t n) {
    this->stop();
    memcpy(s + 1, _time.get(), LogTime::t_len);

    this->write_logs(s, n);
    if (!FLG_also_log2console) _cerr(s, n);
    mod().log_fatal->write(s, n);

    co::atomic_store(&g_has_fatal_log, true);
    abort();
}

void Logger::write_logs(void* p, size_t n) {
    if (!g_write_cb || g_also_log2local) _file.write(p, n);
    if (g_write_cb) g_write_cb(p, n);
    if (FLG_also_log2console) _cerr(p, n);
}

void Logger::thread_fun() {
    bool signaled;

    while (!_stop) {
        signaled = _log_event.wait(FLG_log_flush_ms);
        if (_stop) break;

        _time.update();
        if (FLG_log_daily && _time.day() != g_day) {
            g_day_changed = true;
            g_day = _time.day();
            g_last_day = *(uint32*)_x.time_str;
            memcpy(g_last_time, _x.time_str, 24);
        }

        {
            std::lock_guard<std::mutex> g(_x.mtx);
            memcpy(_x.time_str, _time.get(), LogTime::t_len);
            if (!_x.buf.empty()) _x.buf.swap(_buf);
        }

        if (!_buf.empty()) {
            this->write_logs(_buf.data(), _buf.size());
            _buf.clear();
        }

        if (FLG_log_daily && g_day_changed) g_day_changed = false;
        if (signaled) _log_event.reset();
    }

    co::atomic_store(&_stop, 2);
}

#ifdef WITH_BACKTRACE
struct StackTrace {
    typedef void (*write_cb_t)(void*, size_t);
    static const size_t N = 4096; // demangle buffer size
    StackTrace()
        : _f(0), _buf((char*)::malloc(N)), _size(N), _s(N), _exe(os::exepath()) {
        memset(_buf, 0, N);
        memset(_s.data(), 0, _s.capacity());
        (void) _exe.c_str();
    }

    ~StackTrace() {
        if (_buf) { ::free(_buf); _buf = nullptr; }
    }

    void dump_stack(void* f, int skip);
    char* demangle(const char* name);
    int backtrace(const char* file, int line, const char* func, int& count);

    write_cb_t _f;
    char* _buf;    // for demangle
    size_t _size;  // buf size
    fastream _s;   // for stack trace
    fastring _exe; // exe path
};

// https://gcc.gnu.org/onlinedocs/libstdc++/libstdc++-html-USERS-4.3/a01696.html
char* StackTrace::demangle(const char* name) {
    int status = 0;
    size_t n = _size;
    char* p = abi::__cxa_demangle(name, _buf, &n, &status);
    if (_size < n) { /* buffer reallocated, not likely to happen */
        _buf = p;
        _size = n;
    }
    return p;
}

struct user_data_t {
    StackTrace* st;
    int count;
};

void error_cb(void* data, const char* msg, int errnum) {
    _cerr(msg);
    _cerr("\n", 1);
}

int backtrace_cb(void* data, uintptr_t /*pc*/, const char* file, int line, const char* func) {
    user_data_t* ud = (user_data_t*)data;
    return ud->st->backtrace(file, line, func, ud->count);
}

void StackTrace::dump_stack(void* f, int skip) {
    _f = (write_cb_t)f;
    struct user_data_t ud = { this, 0 };
    struct backtrace_state* state = backtrace_create_state(_exe.c_str(), 1, error_cb, NULL);
    backtrace_full(state, skip, backtrace_cb, error_cb, (void*)&ud);
}

int StackTrace::backtrace(const char* file, int line, const char* func, int& count) {
    if (!file && !func) return 0;
    if (func) {
        char* p = this->demangle(func);
        if (p) func = p;
    }

    _s.clear();
    _s << '#' << (count++) << "  in " << (func ? func : "???") << " at " 
       << (file ? file : "???") << ':' << line << '\n';

    if (_f) _f(_s.data(), _s.size());
    return 0;
}

#else
struct StackTrace {
    void dump_stack(void*, int) {}
};
#endif

struct ExceptHandler {
    ExceptHandler();
    ~ExceptHandler();

    void handle_signal(int sig);
    int handle_exception(void* e); // for windows only

    StackTrace* _stack_trace;
    co::map<int, os::sig_handler_t> _old_handlers;
};

void on_signal(int sig) {
    mod().except_handler->handle_signal(sig);
}

#ifdef _WIN32
LONG WINAPI on_except(PEXCEPTION_POINTERS p) {
    return mod().except_handler->handle_exception((void*)p);
}
#endif

static void write_except_info(void* p, size_t n) {
    _cerr(p, n);
    mod().log_file->write(p, n);
    mod().log_fatal->write(p, n);
}

ExceptHandler::ExceptHandler() {
    _stack_trace = co::_make_static<StackTrace>();
    _old_handlers[SIGINT] = os::signal(SIGINT, on_signal);
    _old_handlers[SIGTERM] = os::signal(SIGTERM, on_signal);
    _old_handlers[SIGABRT] = os::signal(SIGABRT, on_signal);
#ifdef _WIN32
    // Signal handler for SIGSEGV and SIGFPE installed in main thread does 
    // not work for other threads. Use SetUnhandledExceptionFilter instead.
    SetUnhandledExceptionFilter(on_except);
#else
    const int x = SA_RESTART; // | SA_ONSTACK;
    _old_handlers[SIGQUIT] = os::signal(SIGQUIT, on_signal);
    _old_handlers[SIGSEGV] = os::signal(SIGSEGV, on_signal, x);
    _old_handlers[SIGFPE] = os::signal(SIGFPE, on_signal, x);
    _old_handlers[SIGBUS] = os::signal(SIGBUS, on_signal, x);
    _old_handlers[SIGILL] = os::signal(SIGILL, on_signal, x);
    os::signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE
#endif
}

ExceptHandler::~ExceptHandler() {
    os::signal(SIGINT, SIG_DFL);
    os::signal(SIGTERM, SIG_DFL);
    os::signal(SIGABRT, SIG_DFL);
#ifndef _WIN32
    os::signal(SIGQUIT, SIG_DFL);
    os::signal(SIGSEGV, SIG_DFL);
    os::signal(SIGFPE, SIG_DFL);
    os::signal(SIGBUS, SIG_DFL);
    os::signal(SIGILL, SIG_DFL);
#endif
}

#if defined(_WIN32) && !defined(SIGQUIT)
#define SIGQUIT SIGTERM
#endif

void ExceptHandler::handle_signal(int sig) {
    auto& m = mod();
    if (sig == SIGINT || sig == SIGTERM || sig == SIGQUIT) {
        m.logger->stop(true);
        os::signal(sig, _old_handlers[sig]);
        raise(sig);
        return;
    }

    m.logger->stop(true);
    auto& s = *m.stream; s.clear();
    if (!g_has_fatal_log) {
        g_has_exception = true;
        s << 'F' << m.log_time->get() << ' ' << co::thread_id() << "] ";
    }

    switch (sig) {
        case SIGABRT:
            if (!g_has_fatal_log) s << "SIGABRT: aborted\n";
            break;
    #ifndef _WIN32
        case SIGSEGV:
            s << "SIGSEGV: segmentation fault\n";
            break;
        case SIGFPE:
            s << "SIGFPE: floating point exception\n";
            break;
        case SIGBUS:
            s << "SIGBUS: bus error\n";
            break;
        case SIGILL:
            s << "SIGILL: illegal instruction\n";
            break;
    #endif
    }

    if (!s.empty()) write_except_info(s.data(), s.size());
    if (_stack_trace) {
        const int skip = g_has_fatal_log ? 7 : (sig == SIGABRT ? 4 : 3);
        _stack_trace->dump_stack((void*)write_except_info, skip);
    }

    os::signal(sig, _old_handlers[sig]);
    raise(sig);
}

#ifdef _WIN32
#define CASE_EXCEPT(e) case e: err = #e; break

int ExceptHandler::handle_exception(void* e) {
    auto& m = mod();
    const char* err = nullptr;
    auto p = (PEXCEPTION_POINTERS)e;

    switch (p->ExceptionRecord->ExceptionCode) {
        CASE_EXCEPT(EXCEPTION_ACCESS_VIOLATION);
        CASE_EXCEPT(EXCEPTION_ARRAY_BOUNDS_EXCEEDED);
        CASE_EXCEPT(EXCEPTION_DATATYPE_MISALIGNMENT);
        CASE_EXCEPT(EXCEPTION_FLT_DIVIDE_BY_ZERO);
        CASE_EXCEPT(EXCEPTION_ILLEGAL_INSTRUCTION);
        CASE_EXCEPT(EXCEPTION_INT_DIVIDE_BY_ZERO);
        CASE_EXCEPT(EXCEPTION_NONCONTINUABLE_EXCEPTION);
        CASE_EXCEPT(EXCEPTION_STACK_OVERFLOW);
        CASE_EXCEPT(STATUS_INVALID_HANDLE);
        CASE_EXCEPT(STATUS_STACK_BUFFER_OVERRUN);
        case 0xE06D7363: // std::runtime_error()
            err = "STATUS_CPP_EH_EXCEPTION";
            break;
        case 0xE0434f4D: // VC++ Runtime error
            err = "STATUS_CLR_EXCEPTION";
            break;
        case 0xCFFFFFFF:
            err = "STATUS_APPLICATION_HANG";
            break;
        default:
            err = "Unexpected exception: ";
            break;
    }

    m.logger->stop();
    auto& s = *m.stream; s.clear();
    s << 'F' << m.log_time->get() << "] " << err;
    if (*err == 'U') s << (void*)(size_t)p->ExceptionRecord->ExceptionCode;
    s << '\n';

    write_except_info(s.data(), s.size());
    if (_stack_trace) {
        _stack_trace->dump_stack((void*)write_except_info, 6);
    }

    ::exit(0);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif // _WIN32

Mod::Mod() {
    _init_log_dir();
    exename = co::_make_static<fastring>(os::exename());
    stream = co::_make_static<fastring>(4096);
    log_time = co::_make_static<LogTime>();
    log_file = co::_make_static<LogFile>(false);
    log_fatal = co::_make_static<LogFile>(true);
    logger = co::_make_static<Logger>(log_time, log_file);
    except_handler = co::_make_static<ExceptHandler>();
}

static void unhide_flags() {
    flag::set_attr("log_dir", flag::attr_default);
    flag::set_attr("min_log_level", flag::attr_default);
    flag::set_attr("max_log_size", flag::attr_default);
    flag::set_attr("max_log_file_size", flag::attr_default);
    flag::set_attr("max_log_file_num", flag::attr_default);
    flag::set_attr("max_log_buffer_size", flag::attr_default);
    flag::set_attr("log_flush_ms", flag::attr_default);
    flag::set_attr("also_log2console", flag::attr_default);
    flag::set_attr("log_daily", flag::attr_default);
}

static void init() {
    do {
        // ensure max_log_buffer_size >= 1M, max_log_size >= 256
        auto& b = FLG_max_log_buffer_size;
        auto& l = FLG_max_log_size;
        auto& f = FLG_max_log_file_size;
        if (b < (1 << 20)) b = 1 << 20;
        if (l < 256) l = 256;
        if (l > (b >> 2)) l = b >> 2;
        if (f <= 0) f = 256 << 20;
    } while (0);

    g_mod->logger->start();
}

static int g_nifty_counter;
LogInit::LogInit() {
    const int n = ++g_nifty_counter;
    if (n == 1) {
        g_mod = co::_make_static<Mod>();
    }
    if (n == 2) {
        flag::run_before_parse(unhide_flags);
        flag::run_after_parse(init);
    }
}

static __thread fastream* g_s;

inline fastream& log_stream() {
    return g_s ? *g_s : *(g_s = co::_make_static<fastream>(256));
}

LogSaver::LogSaver(const char* fname, unsigned fnlen, unsigned line, int level)
    : s(log_stream()) {
    n = s.size();
    s.resize(n + (LogTime::t_len + 1)); // make room for: "I0523 17:00:00.123"
    s[n] = "DIWE"[level];
    (s << ' ' << co::thread_id() << ' ').append(fname, fnlen) << ':' << line << "] ";
}

LogSaver::~LogSaver() {
    s << '\n';
    mod().logger->push_normal_log(s.data() + n, s.size() - n);
    s.resize(n);
}

FatalLogSaver::FatalLogSaver(const char* fname, unsigned fnlen, unsigned line)
    : s(log_stream()) {
    s.resize(LogTime::t_len + 1);
    s.front() = 'F';
    (s << ' ' << co::thread_id() << ' ').append(fname, fnlen) << ':' << line << "] ";
}

FatalLogSaver::~FatalLogSaver() {
    s << '\n';
    mod().logger->push_fatal_log(s.data(), s.size());
}

} // xx

void set_write_cb(void(*cb)(void*, size_t), bool also_log2local) {
    g_write_cb = cb;
    g_also_log2local = also_log2local;
}

void close() {
    xx::mod().logger->stop();
}

} // log
} // _xx

#ifdef _WIN32
LONG WINAPI _co_on_exception(PEXCEPTION_POINTERS p) {
    return _xx::log::xx::on_except(p);
}
#endif
