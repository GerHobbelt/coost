#pragma once

#include "fastring.h"

namespace co {

// The following characters are not encoded:
//   - reserved:  ! ( ) * # $ & ' + , / : ; = ? @ [ ] 
//   - a-z  A-Z  0-9  - _ . ~ 
fastring url_encode(const void* s, size_t n);

fastring url_decode(const void* s, size_t n);

inline fastring url_encode(const char* s) {
    return url_encode(s, strlen(s));
}

inline fastring url_encode(const fastring& s) {
    return url_encode(s.data(), s.size());
}

inline fastring url_encode(const std::string& s) {
    return url_encode(s.data(), s.size());
}

inline fastring url_decode(const char* s) {
    return url_decode(s, strlen(s));
}

inline fastring url_decode(const fastring& s) {
    return url_decode(s.data(), s.size());
}

inline fastring url_decode(const std::string& s) {
    return url_decode(s.data(), s.size());
}

} // co
