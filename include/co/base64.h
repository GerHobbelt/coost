#pragma once

#include "fastring.h"

namespace co {

fastring base64_encode(const void* s, size_t n);

// return empty string on any error 
fastring base64_decode(const void* s, size_t n);

inline fastring base64_encode(const char* s) {
    return base64_encode(s, strlen(s));
}

inline fastring base64_encode(const fastring& s) {
    return base64_encode(s.data(), s.size());
}

inline fastring base64_encode(const std::string& s) {
    return base64_encode(s.data(), s.size());
}

inline fastring base64_decode(const char* s) {
    return base64_decode(s, strlen(s));
}

inline fastring base64_decode(const fastring& s) {
    return base64_decode(s.data(), s.size());
}

inline fastring base64_decode(const std::string& s) {
    return base64_decode(s.data(), s.size());
}

} // co
