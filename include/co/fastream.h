#pragma once

#include "stream.h"
#include "fastring.h"

struct fastream : co::stream {
    constexpr fastream() noexcept
        : co::stream() {
    }
    
    explicit fastream(size_t cap)
        : co::stream(cap) {
    }

    ~fastream() = default;

    fastream(const fastream&) = delete;
    void operator=(const fastream&) = delete;

    fastream(fastream&& fs) noexcept
        : co::stream(std::move(fs)) {
    }

    fastream& operator=(fastream&& fs) {
        return (fastream&) co::stream::operator=(std::move(fs));
    }

    fastring str() const {
        return fastring(_p, _size);
    }

    fastream& append(const void* p, size_t n) {
        return (fastream&) co::stream::append(p, n);
    }

    fastream& append_nomchk(const void* p, size_t n) {
        return (fastream&) co::stream::append_nomchk(p, n);
    }

    fastream& append(const char* s) {
        return this->append(s, strlen(s));
    }

    fastream& append_nomchk(const char* s) {
        return this->append_nomchk(s, strlen(s));
    }

    fastream& append(const fastring& s) {
        return this->append_nomchk(s.data(), s.size());
    }

    fastream& append(const std::string& s) {
        return this->append_nomchk(s.data(), s.size());
    }

    fastream& append(const fastream& s) {
        if (&s != this) return this->append_nomchk(s.data(), s.size());
        this->reserve((_size << 1) + !!_size);
        memcpy(_p + _size, _p, _size); // append itself
        _size <<= 1;
        return *this;
    }

    fastream& append(size_t n, char c) {
        return (fastream&) co::stream::append(n, c);
    }

    fastream& append(char c) {
        return (fastream&) co::stream::append(c);
    }

    fastream& append(signed char c) {
        return this->append((char)c);
    }

    fastream& append(unsigned char c) {
        return this->append((char)c);
    }

    fastream& append(uint16 v) {
        return this->append_nomchk(&v, sizeof(v));
    }

    fastream& append(uint32 v) {
        return this->append_nomchk(&v, sizeof(v));
    }

    fastream& append(uint64 v) {
        return this->append_nomchk(&v, sizeof(v));
    }

    fastream& cat() noexcept { return *this; }

    // concatenate fastream to any number of elements
    //   - fastream s("hello");
    //     s.cat(' ', 123);  // s -> "hello 123"
    template<typename X, typename ...V>
    fastream& cat(X&& x, V&& ... v) {
        (*this) << std::forward<X>(x);
        return this->cat(std::forward<V>(v)...);
    }

    fastream& operator<<(bool v) {
        return (fastream&) co::stream::operator<<(v);
    }

    fastream& operator<<(char v) {
        return (fastream&) co::stream::operator<<(v);
    }

    fastream& operator<<(signed char v) {
        return this->operator<<((char)v);
    }

    fastream& operator<<(unsigned char v) {
        return this->operator<<((char)v);
    }

    fastream& operator<<(short v) {
        return (fastream&) co::stream::operator<<(v);
    }

    fastream& operator<<(unsigned short v) {
        return (fastream&) co::stream::operator<<(v);
    }

    fastream& operator<<(int v) {
        return (fastream&) co::stream::operator<<(v);
    }

    fastream& operator<<(unsigned int v) {
        return (fastream&) co::stream::operator<<(v);
    }

    fastream& operator<<(long v) {
        return (fastream&) co::stream::operator<<(v);
    }

    fastream& operator<<(unsigned long v) {
        return (fastream&) co::stream::operator<<(v);
    }

    fastream& operator<<(long long v) {
        return (fastream&) co::stream::operator<<(v);
    }

    fastream& operator<<(unsigned long long v) {
        return (fastream&) co::stream::operator<<(v);
    }

    fastream& operator<<(double v) {
        return (fastream&) co::stream::operator<<(v);
    }

    fastream& operator<<(float v) {
        return (fastream&) co::stream::operator<<(v);
    }

    // float point number with max decimal places set
    //   - fastream() << co::dp::_2(3.1415);  // -> 3.14
    fastream& operator<<(const co::dp::D& v) {
        return (fastream&) co::stream::operator<<(v);
    }

    fastream& operator<<(const void* v) {
        return (fastream&) co::stream::operator<<(v);
    }

    fastream& operator<<(std::nullptr_t) {
        return (fastream&) co::stream::operator<<(nullptr);
    }

    fastream& operator<<(const char* s) {
        return this->append(s, strlen(s));
    }

    fastream& operator<<(const signed char* s) {
        return this->operator<<((const char*)s);
    }

    fastream& operator<<(const unsigned char* s) {
        return this->operator<<((const char*)s);
    }

    fastream& operator<<(const fastring& s) {
        return this->append_nomchk(s.data(), s.size());
    }

    fastream& operator<<(const std::string& s) {
        return this->append_nomchk(s.data(), s.size());
    }

    fastream& operator<<(const fastream& s) {
        return this->append(s);
    }
};
