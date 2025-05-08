#pragma once

#include "co/def.h"
#include "fastream.h"
#include <iostream>

namespace co {
namespace lang {

enum Lang {
    chs,
    eng,
};

extern Lang g_lang;

} // lang


inline void set_language(lang::Lang lang) {
    lang::g_lang = lang;
}

struct mlstr {
    mlstr() = delete;
    ~mlstr() = delete;
    mlstr(mlstr&&) = delete;
    mlstr(const mlstr&) = delete;
    void operator=(const mlstr&) = delete;
    void operator=(mlstr&&) = delete;

    operator const char*() const noexcept {
        return this->value();
    }

    const char* value() const noexcept {
        return (&_s)[lang::g_lang];
    }

    const char* _s;
};

} // co

inline std::ostream& operator<<(std::ostream& os, const co::mlstr& s) {
    return os << s.value();
}

inline fastream& operator<<(fastream& fs, const co::mlstr& s) {
    return fs << s.value();
}

#define DEF_mlstr(name, s_chs, s_eng) \
    static const char* _MLS_v_##name[] = { s_chs, s_eng }; \
    static const co::mlstr& MLS_##name = *(co::mlstr*)_MLS_v_##name;
