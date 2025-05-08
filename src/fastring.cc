#include "co/fastring.h"

fastring& fastring::trim(char c, char d) {
    if (!this->empty()) {
        size_t b, e;
        switch (d) {
        case 'r':
        case 'R':
            e = _size;
            while (e > 0 && _p[e - 1] == c) --e;
            if (e != _size) _size = e;
            break;
        case 'l':
        case 'L':
            b = 0;
            while (b < _size && _p[b] == c) ++b;
            if (b != 0 && (_size -= b) != 0) memmove(_p, _p + b, _size);
            break;
        default:
            b = 0, e = _size;
            while (e > 0 && _p[e - 1] == c) --e;
            if (e != _size) _size = e;
            while (b < _size && _p[b] == c) ++b;
            if (b != 0 && (_size -= b) != 0) memmove(_p, _p + b, _size);
            break;
        }
    }
    return *this;
}

fastring& fastring::trim(const char* x, char d) {
    if (!this->empty() && x && *x) {
        const unsigned char* s = (const unsigned char*)x;
        const unsigned char* const p = (const unsigned char*)_p;
        unsigned char bs[256] = { 0 };
        while (*s) bs[*s++] = 1;

        size_t b, e;
        switch (d) {
        case 'r':
        case 'R':
            e = _size;
            while (e > 0 && bs[p[e - 1]]) --e;
            if (e != _size) _size = e;
            break;
        case 'l':
        case 'L':
            b = 0;
            while (b < _size && bs[p[b]]) ++b;
            if (b != 0 && (_size -= b) != 0) memmove(_p, _p + b, _size);
            break;
        default:
            b = 0, e = _size;
            while (e > 0 && bs[p[e - 1]]) --e;
            if (e != _size) _size = e;
            while (b < _size && bs[p[b]]) ++b;
            if (b != 0 && (_size -= b) != 0) memmove(_p, _p + b, _size);
            break;
        }
    }
    return *this;
}

fastring& fastring::trim(size_t n, char d) {
    if (!this->empty()) {
        switch (d) {
        case 'r':
        case 'R':
            _size = n < _size ? _size - n : 0;
            break;
        case 'l':
        case 'L':
            if (n < _size) {
                _size -= n;
                memmove(_p, _p + n, _size);
            } else {
                _size = 0;
            }
            break;
        default:
            if (n * 2 < _size) {
                _size -= n * 2;
                memmove(_p, _p + n, _size);
            } else {
                _size = 0;
            }
        }
    }
    return *this;
}

fastring& fastring::replace(const char* sub, size_t n, const char* to, size_t m, size_t maxreplace) {
    if (!this->empty() && n > 0) {
        const char* p = co::memmem(_p, _size, sub, n);
        if (p) {
            const char* from = _p;
            const char* const e = _p + _size;
            fastring s(_size + 1);
            do {
                s.append(from, p - from).append(to, m);
                from = p + n;
                if (maxreplace && --maxreplace == 0) break;
            } while ((p = co::memmem(from, e - from, sub, n)));

            if (from < _p + _size) s.append(from, e - from);
            this->swap(s);
        }
    }
    return *this;
}

fastring& fastring::escape() {
    const char* b = _p;
    const char* const e = _p + _size;
    fastring s;
    for (const char* p = b; p < e; ++p) {
        char d;
        const char c = *p;
        switch (c) {
        case '"':  d = '"';  break;
        case '\\': d = '\\'; break;
        case '\0': d = '0';  break;
        case '\r': d = 'r';  break;
        case '\n': d = 'n';  break;
        case '\t': d = 't';  break;
        case '\a': d = 'a';  break;
        case '\b': d = 'b';  break;
        case '\f': d = 'f';  break;
        case '\v': d = 'v';  break;
        default: continue;
        }
        if (s.empty()) s.reserve(s.size() + 8);
        s.append(b, p - b).append('\\').append(d);
        b = p + 1;
    }

    if (!s.empty()) {
        if (b < e) s.append(b, e - b);
        this->swap(s);
    }
    return *this;
}

fastring& fastring::unescape() {
    const char* b = _p;
    const char* const e = _p + _size;
    fastring s;
    for (const char* p = (char*)memchr(b, '\\', e - b); p && p + 1 < e;) {
        char d;
        const char c = *(p + 1);
        switch (c) {
        case '"':  d = '"';  break;
        case '\'': d = '\''; break;
        case '\\': d = '\\'; break;
        case '0':  d = '\0'; break;
        case 'r':  d = '\r'; break;
        case 'n':  d = '\n'; break;
        case 't':  d = '\t'; break;
        case 'a':  d = '\a'; break;
        case 'b':  d = '\b'; break;
        case 'f':  d = '\f'; break;
        case 'v':  d = '\v'; break;
        default:
            p = (char*)memchr(p + 2, '\\', e - p - 2);
            continue;
        }
        if (s.empty()) s.reserve(s.size());
        s.append(b, p - b).append(d);
        b = p + 2;
        p = (char*)memchr(b, '\\', e - b);
    }

    if (!s.empty()) {
        if (b < e) s.append(b, e - b);
        this->swap(s);
    }
    return *this;
}

fastring& fastring::toupper() {
    for (size_t i = 0; i < _size; ++i) {
        char& c = _p[i];
        if ('a' <= c && c <= 'z') c ^= 32;
    }
    return *this;
}

fastring& fastring::tolower() {
    for (size_t i = 0; i < _size; ++i) {
        char& c = _p[i];
        if ('A' <= c && c <= 'Z') c ^= 32;
    }
    return *this;
}

size_t fastring::find_first_of(const char* s, size_t pos, size_t n) const {
    if (pos < _size && n > 0) {
        unsigned char bs[256] = { 0 };
        for (size_t i = 0; i < n; ++i) bs[(unsigned char)s[i]] = 1;
        for (size_t i = pos; i < _size; ++i) {
            if (bs[(unsigned char)_p[i]]) return i;
        }
    }
    return npos;
}

size_t fastring::find_first_not_of(const char* s, size_t pos, size_t n) const {
    if (pos < _size) {
        unsigned char bs[256] = { 0 };
        for (size_t i = 0; i < n; ++i) bs[(unsigned char)s[i]] = 1;
        for (size_t i = pos; i < _size; ++i) {
            if (!bs[(unsigned char)_p[i]]) return i;
        }
    }
    return npos;
}

size_t fastring::find_first_not_of(char c, size_t pos) const {
    for (; pos < _size; ++pos) {
        if (_p[pos] != c) return pos;
    }
    return npos;
}

size_t fastring::find_last_of(const char* s, size_t pos, size_t n) const {
    if (_size > 0 && n > 0) {
        unsigned char bs[256] = { 0 };
        for (size_t i = 0; i < n; ++i) bs[(unsigned char)s[i]] = 1;
        for (size_t i = (pos >= _size ? _size : (pos + 1)); i > 0;) {
            if (bs[(unsigned char)_p[--i]]) return i;
        }
    }
    return npos;
}

size_t fastring::find_last_not_of(const char* s, size_t pos, size_t n) const {
    if (_size > 0) {
        unsigned char bs[256] = { 0 };
        for (size_t i = 0; i < n; ++i) bs[(unsigned char)s[i]] = 1;
        for (size_t i = (pos >= _size ? _size : (pos + 1)); i > 0;) {
            if (!bs[(unsigned char)_p[--i]]) return i;
        }
    }
    return npos;
}

size_t fastring::find_last_not_of(char c, size_t pos) const {
    if (_size > 0) {
        for (size_t i = (pos >= _size ? _size : (pos + 1)); i > 0;) {
            if (_p[--i] != c) return i;
        }
    }
    return npos;
}
