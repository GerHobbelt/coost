#pragma once

#include "fastream.h"
#include <iostream>

namespace co {

// multi-language string
struct mls {
    enum lang_t {
        chs,
        eng,
    };

    mls() = delete;
    ~mls() = delete;
    mls(mls&&) = delete;
    mls(const mls&) = delete;
    void operator=(const mls&) = delete;
    void operator=(mls&&) = delete;

    operator const char*() const noexcept {
        return this->value();
    }

    const char* value() const noexcept {
        return (&_s)[g_lang];
    }

    static void set_lang_chs() { g_lang = chs; }
    static void set_lang_eng() { g_lang = eng; }

    const char* _s;
    static lang_t g_lang;
};

} // co

inline std::ostream& operator<<(std::ostream& os, const co::mls& s) {
    return os << s.value();
}

inline fastream& operator<<(fastream& fs, const co::mls& s) {
    return fs << s.value();
}

#define DEF_mls(name, s_chs, s_eng) \
    static const char* _MLS_v_##name[] = { s_chs, s_eng }; \
    static const co::mls& MLS_##name = *(co::mls*)_MLS_v_##name;
