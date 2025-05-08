#include "co/mem.h"
#include "./mem.h"
#include "co/atomic.h"
#include "co/god.h"
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <memoryapi.h>
#else
#include <sys/mman.h>
#endif


#ifdef _WIN32
inline void* _vm_reserve(size_t n) {
    return VirtualAlloc(NULL, n, MEM_RESERVE, PAGE_READWRITE);
}

inline bool _vm_commit(void* p, size_t n) {
    return VirtualAlloc(p, n, MEM_COMMIT, PAGE_READWRITE) == p;
}

inline void _vm_decommit(void* p, size_t n) {
    VirtualFree(p, n, MEM_DECOMMIT);
}

inline void _vm_free(void* p, size_t n) {
    VirtualFree(p, 0, MEM_RELEASE);
}

#else
inline void* _vm_reserve(size_t n) {
    void* const p = ::mmap(
        NULL, n, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0
    );
    return p != MAP_FAILED ? p : NULL;
}

inline bool _vm_commit(void* p, size_t n) {
    return ::mmap(
        p, n, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0
    ) == p;
}

inline void _vm_decommit(void* p, size_t n) {
    (void) ::mmap(
        p, n, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0
    );
}

inline void _vm_free(void* p, size_t n) {
    ::munmap(p, n);
}
#endif

namespace co {

StaticMem::~StaticMem() {
    _l.for_each([](clink* c) { ::free(c); });
    _l.clear();
}

void* StaticMem::alloc(uint32 n, uint32 align) {
    if (!_l.empty()) goto _1;
    _l.push_front((Memb*) ::malloc(_blk_size));

_1:
    char* p = _h->p + _pos;
    if (align != sizeof(void*)) p = god::align_up(p, align);

    n = god::align_up(n, align);
    if (p + n <= (char*)_h + _blk_size) goto _2;

    _l.push_front((Memb*) ::malloc(_blk_size));
    _pos = 0;
    p = align != sizeof(void*) ? god::align_up(_h->p, align) : _h->p;

_2:
    _pos = god::cast<uint32>(p - _h->p) + n;
    return p;
}

Dealloc::~Dealloc() {
    Memb* const h = _m._h;
    if (h) {
        const auto off = (uint32)(god::align_up(h->p, A) - (char*)h);
        for (Memb* b = h; b; b = (Memb*)b->next) {
            destruct_t* p = (destruct_t*) ((char*)b + off);
            const uint32 n = ((b != h ? _m._blk_size : _m._pos) - off) / N;
            for (uint32 x = n; x > 0; --x) {
                auto& d = p[x - 1];
                d();
                d.~destruct_t();
            }
        }
    }
}

struct StaticAlloc {
    StaticAlloc(uint32 blk_size) : _m(blk_size) {}
    ~StaticAlloc() = default;

    void* alloc(size_t n) {
        const size_t H = co::cache_line_size >> 1;
        return _m.alloc((uint32)n, n <= H ? sizeof(void*) : co::cache_line_size);
    }

    StaticMem _m;
};

struct Root {
    Root() : _mtx(), _sa(8192) {}
    ~Root() = default;

    template<typename T, typename... Args>
    T* make(Args&&... args) {
        void* p;
        {
            std::lock_guard<std::mutex> g(_mtx);
            p = _sa.alloc(sizeof(T));
            if (p) _da.add_destructor([p](){ ((T*)p)->~T(); });
        }
        return p ? new(p) T(std::forward<Args>(args)...) : 0;
    }

    void add_destructor(destruct_t&& f, int i) {
        std::lock_guard<std::mutex> g(_mtx);
        _dx[i].add_destructor(std::forward<destruct_t>(f));
    }

    std::mutex _mtx;
    StaticAlloc _sa; // alloc memory for GlobalAlloc and ThreadAlloc
    Dealloc _da;     // used to destruct GlobalAlloc and ThreadAlloc
    Dealloc _dx[4];  // 0: _rootic, 1: _static, 2: rootic, 3: static 
};

#if __arch64
static const uint32 B = 6;
static const uint32 g_array_size = 32;
#else
static const uint32 B = 5;
static const uint32 g_array_size = 4;
#endif

static const uint32 CL = co::cache_line_size;
static const uint32 R = (1 << B) - 1;
static const size_t C = (size_t)1;
static const uint32 g_sb_bits = 15;
static const uint32 g_lb_bits = g_sb_bits + B;
static const uint32 g_hb_bits = g_lb_bits + B;
static const size_t g_max_alloc_size = 1u << 17; // 128k

struct Bitset {
    explicit Bitset(void* s) : _s((size_t*)s) {}

    void set(uint32 i) {
        _s[i >> B] |= (C << (i & R));
    }

    void unset(uint32 i) {
        _s[i >> B] &= ~(C << (i & R));
    }

    bool test_and_unset(uint32 i) {
        const size_t x = (C << (i & R));
        return god::fetch_and(&_s[i >> B], ~x) & x;
    }

    int rfind(uint32 i) const {
        int n = static_cast<int>(i >> B);
        do {
            const size_t x = _s[n];
            if (x) return _find_msb(x) + (n << B);
        } while (--n >= 0);
        return -1;
    }

    void atomic_set(uint32 i) {
        co::atomic_or(&_s[i >> B], C << (i & R), mo_relaxed);
    }

    size_t* _s;
};

// 128M on arch64, or 32M on arch32
// manage and alloc large blocks(2M or 1M)
struct HugeBlock : co::clink {
    explicit HugeBlock(void* p) : _p((char*)p) {
    }

    void* alloc() {
        const uint32 i = _find_lsb(~_bits);
        if (i < R) {
            _bits |= (C << i);
            return _p + (((size_t)i) << g_lb_bits);
        }
        return NULL;
    }

    bool free(void* p) {
        const uint32 i = (uint32)(((char*)p - _p) >> g_lb_bits);
        return (_bits &= ~(C << i)) == 0;
    }

    char* _p; // beginning address to alloc
    size_t _bits;
    DISALLOW_COPY_AND_ASSIGN(HugeBlock);
};

inline HugeBlock* make_huge_block() {
    void* x = _vm_reserve(1u << g_hb_bits);
    if (x) {
        if (_vm_commit(x, 4096)) {
            void* p = god::align_up<(1u << g_lb_bits)>(x);
            if (p == x) p = (char*)x + (1u << g_lb_bits);
            return new (x) HugeBlock(p);
        }
        _vm_free(x, 1u << g_hb_bits);
    }
    return NULL;
}

// 2M on arch64, or 1M on arch32
// manage and alloc small blocks(32K)
struct LargeBlock : co::clink {
    explicit LargeBlock(HugeBlock* parent)
        : _p((char*)this + (1u << g_sb_bits)), _parent(parent) {
    }

    void* alloc() {
        const uint32 i = _find_lsb(~_bits);
        if (i < R) {
            _bits |= (C << i);
            return _p + (((size_t)i) << g_sb_bits);
        }
        return NULL;
    }

    bool free(void* p) {
        const uint32 i = (uint32)(((char*)p - _p) >> g_sb_bits);
        return (_bits &= ~(C << i)) == 0;
    }

    HugeBlock* parent() const { return _parent; }

    char* _p; // beginning address to alloc
    size_t _bits;
    HugeBlock* _parent;
    DISALLOW_COPY_AND_ASSIGN(LargeBlock);
};

// thread-local allocator
struct ThreadAlloc;

// alloc memory from 4K to 128K
struct LargeAlloc : co::clink {
    static const uint32 BLK_BITS = 12;
    static const uint32 BS_BITS = 1u << (g_lb_bits - BLK_BITS);
    static const uint32 BS_SIZE = BS_BITS >> 3;
    static const uint32 LA_SIZE = 1 << B;
    static const uint32 MAX_BIT = BS_BITS - 1;

    explicit LargeAlloc(HugeBlock* parent, ThreadAlloc* ta)
        : _parent(parent), _ta(ta) {
        static_assert(sizeof(*this) == LA_SIZE, "");
        static_assert(BS_SIZE == LA_SIZE, "");
        static_assert(CL <= 1024, "");
        static_assert((CL & (CL - 1)) == 0, "");
        _p = (char*)this + (1 << BLK_BITS);
        _pbs = (char*)this + LA_SIZE;
        _xpbs = (char*)this + god::align_up<CL>(LA_SIZE + BS_SIZE);
    }

    // alloc n units
    void* alloc(uint32 n) {
        if (_bit + n <= MAX_BIT) {
            _bs.set(_bit);
            return _p + (god::fetch_add(&_bit, n) << BLK_BITS);
        }
        return NULL;
    }

    void* try_hard_alloc(uint32 n);

    bool free(void* p) {
        int i = (int)(((char*)p - _p) >> BLK_BITS);
        //if (!_bs.test_and_unset((uint32)i)) ::abort();
        _bs.unset(i);
        const int r = _bs.rfind(_bit);
        return r < i ? ((_bit = r >= 0 ? i : 0) == 0) : false;
    }

    void xfree(void* p) {
        const uint32 i = (uint32)(((char*)p - _p) >> BLK_BITS);
        _xbs.atomic_set(i);
    }

    void* realloc(void* p, uint32 o, uint32 n) {
        uint32 i = (uint32)(((char*)p - _p) >> BLK_BITS);
        if (_bit == i + o && i + n <= MAX_BIT) {
            _bit = i + n;
            return p;
        }
        return NULL;
    }

    HugeBlock* parent() const { return _parent; }
    ThreadAlloc* talloc() const { return _ta; }

    char* _p;    // beginning address to alloc
    uint32 _bit; // current bit
    union {
        Bitset _bs;
        char* _pbs;
    };
    union {
        Bitset _xbs;
        char* _xpbs;
    };
    HugeBlock* _parent;
    ThreadAlloc* _ta;
    DISALLOW_COPY_AND_ASSIGN(LargeAlloc);
};

void* LargeAlloc::try_hard_alloc(uint32 n) {
    size_t* const p = (size_t*)_pbs;
    size_t* const q = (size_t*)_xpbs;

    int i = _bit >> B;
    while (p[i] == 0) --i;
    size_t x = co::atomic_load(&q[i], mo_relaxed);
    if (x) {
        for (;;) {
            if (x) {
                co::atomic_and(&q[i], ~x, mo_relaxed);
                p[i] &= ~x;
                const int lsb = static_cast<int>(_find_lsb(x) + (i << B));
                const int r = _bs.rfind(_bit);
                if (r >= lsb) break;
                _bit = r >= 0 ? lsb : 0;
                if (_bit == 0) break;
            }
            if (--i < 0) break;
            x = co::atomic_load(&q[i], mo_relaxed);
        }
    }

    if (_bit + n <= MAX_BIT) {
        _bs.set(_bit);
        return _p + (god::fetch_add(&_bit, n) << BLK_BITS);
    }
    return NULL;
}

// alloc memory from 16 to 2K
struct SmallAlloc : co::clink {
    static const uint32 BLK_BITS = 4;
    static const uint32 BS_BITS = 1u << (g_sb_bits - BLK_BITS); // 2048
    static const uint32 BS_SIZE = BS_BITS >> 3; // 256
    static const uint32 SA_SIZE = 1 << B;
    static const uint32 SB_SIZE = god::align_up<CL>(SA_SIZE + BS_SIZE);
    static const uint32 SUM_SIZE = god::align_up<CL>(SB_SIZE + BS_SIZE);
    static const uint32 MAX_BIT = BS_BITS - (SUM_SIZE >> BLK_BITS);

    explicit SmallAlloc(LargeBlock* parent, ThreadAlloc* ta)
        : _bit(0), _parent(parent), _ta(ta) {
        static_assert(sizeof(*this) == SA_SIZE, "");
        _p = (char*)this + SUM_SIZE;
        _pbs = (char*)this + SA_SIZE;
        _xpbs = (char*)this + SB_SIZE;
        next = prev = 0;
    }

    // alloc n units
    void* alloc(uint32 n) {
        if (_bit + n <= MAX_BIT) {
            _bs.set(_bit);
            return _p + (god::fetch_add(&_bit, n) << BLK_BITS);
        }
        return NULL;
    }

    void* alloc(uint32 n, uint32 a) {
        void* p = NULL;
        const uint32 bit = (a <= (CL >> BLK_BITS))
            ? god::align_up(_bit, a)
            : god::align_up(_bit, a) + (god::cast<uint32>(god::align_up(_p, a << BLK_BITS) - _p) >> BLK_BITS);

        n = god::align_up(n, a);
        if (bit + n <= MAX_BIT) {
            _bs.set(bit);
            p = _p + (bit << BLK_BITS);
            _bit = bit + n;
        }
        return p;
    }

    void* try_hard_alloc(uint32 n);

    bool free(void* p) {
        const int i = (int)(((char*)p - _p) >> BLK_BITS);
        //if (!_bs.test_and_unset((uint32)i)) ::abort();
        _bs.unset(i);
        const int r = _bs.rfind(_bit);
        return r < i ? ((_bit = r >= 0 ? i : 0) == 0) : false;
    }

    void xfree(void* p) {
        const uint32 i = (uint32)(((char*)p - _p) >> BLK_BITS);
        _xbs.atomic_set(i);
    }

    void* realloc(void* p, uint32 o, uint32 n) {
        uint32 i = (uint32)(((char*)p - _p) >> BLK_BITS);
        if (_bit == i + o && i + n <= MAX_BIT) {
            _bit = i + n;
            return p;
        }
        return NULL;
    }

    LargeBlock* parent() const { return _parent; }
    ThreadAlloc* talloc() const { return _ta; }

    char* _p; // beginning address to alloc
    uint32 _bit;
    union {
        Bitset _bs;
        char* _pbs;
    };
    union {
        Bitset _xbs;
        char* _xpbs;
    };
    LargeBlock* _parent;
    ThreadAlloc* _ta;
    DISALLOW_COPY_AND_ASSIGN(SmallAlloc);
};

void* SmallAlloc::try_hard_alloc(uint32 n) {
    size_t* const p = (size_t*)_pbs;
    size_t* const q = (size_t*)_xpbs;

    int i = _bit >> B;
    while (p[i] == 0) --i;
    size_t x = co::atomic_load(&q[i], mo_relaxed);
    if (x) {
        for (;;) {
            if (x) {
                co::atomic_and(&q[i], ~x, mo_relaxed);
                p[i] &= ~x;
                const int lsb = static_cast<int>(_find_lsb(x) + (i << B));
                const int r = _bs.rfind(_bit);
                if (r >= lsb) break;
                _bit = r >= 0 ? lsb : 0;
                if (_bit == 0) break;
            }
            if (--i < 0) break;
            x = co::atomic_load(&q[i], mo_relaxed);
        }
    }

    if (_bit + n <= MAX_BIT) {
        _bs.set(_bit);
        return _p + (god::fetch_add(&_bit, n) << 4);
    }
    return NULL;
}

// manage huge blocks, and alloc large blocks
//   - shared by all threads
struct GlobalAlloc {
    GlobalAlloc() = default;
    ~GlobalAlloc();

    struct alignas(CL) X {
        X() : mtx(), hb(0) {}
        std::mutex mtx;
        union {
            HugeBlock* hb;
            co::clist lhb;
        };
    };

    void* alloc(uint32 alloc_id, HugeBlock** parent);
    LargeBlock* make_large_block(uint32 alloc_id);
    LargeAlloc* make_large_alloc(uint32 alloc_id);
    void free(void* p, HugeBlock* hb, uint32 alloc_id);

    X _x[g_array_size];
};

GlobalAlloc::~GlobalAlloc() {
    for (uint32 i = 0; i < g_array_size; ++i) {
        std::lock_guard<std::mutex> g(_x[i].mtx);
        HugeBlock *h = _x[i].hb, *next;
        while (h) {
            next = (HugeBlock*) h->next;
            _vm_free(h, 1u << g_hb_bits);
            h = next;
        }
    }
}

static uint32 g_talloc_id = (uint32)-1;

struct alignas(CL) ThreadAlloc {
    ThreadAlloc(GlobalAlloc* ga)
        : _lb(0), _la(0), _sa(0), _ga(ga), _s(16 * 1024) {
        _id = co::atomic_inc(&g_talloc_id, mo_relaxed);
    }
    ~ThreadAlloc() = default;

    uint32 id() const { return _id; }
    void* alloc(size_t n);
    void* alloc(size_t n, size_t align);
    void free(void* p, size_t n);
    void* realloc(void* p, size_t o, size_t n);
    void* try_realloc(void* p, size_t o, size_t n);
    void* salloc(size_t n) { return _s.alloc(n); }

    union { LargeBlock* _lb; co::clist _llb; };
    union { LargeAlloc* _la; co::clist _lla; };
    union { SmallAlloc* _sa; co::clist _lsa; };
    uint32 _id;
    GlobalAlloc* _ga;
    StaticAlloc _s; 
};

__thread ThreadAlloc* g_ta;
static GlobalAlloc* g_ga;
static Root* g_root;
struct alignas(CL) { char _[sizeof(Root)]; } g_root_buf;

namespace xx {

static int g_nifty_counter;

MemInit::MemInit() {
    if (g_nifty_counter++ == 0) {
        g_root = (Root*)&g_root_buf;
        new (g_root) Root();
        g_ga = g_root->make<GlobalAlloc>();
    }
}

MemInit::~MemInit() {
    if (--g_nifty_counter == 0) g_root->~Root();
}

} // xx

inline ThreadAlloc* talloc() {
    return g_ta ? g_ta : (g_ta = g_root->make<ThreadAlloc>(g_ga));
}

#define _try_alloc(l, n, k) \
    const auto h = l.front(); \
    auto k = h->next; \
    l.move_back(h); \
    for (int i = 0; i < n && k != h; k = k->next, ++i)

inline void* GlobalAlloc::alloc(uint32 alloc_id, HugeBlock** parent) {
    void* p = NULL;
    auto& x = _x[alloc_id & (g_array_size - 1)];

    do {
        std::lock_guard<std::mutex> g(x.mtx);
        if (x.hb && (p = x.hb->alloc())) {
            *parent = x.hb;
            goto end;
        }
        if (x.hb && x.hb->next) {
            _try_alloc(x.lhb, 8, k) {
                if ((p = ((HugeBlock*)k)->alloc())) {
                    *parent = (HugeBlock*)k;
                    x.lhb.move_front(k);
                    goto end;
                }
            }
        }
        {
            auto hb = make_huge_block();
            if (hb) {
                x.lhb.push_front(hb);
                p = hb->alloc();
                *parent = hb;
            }
        }
    } while (0);

end:
    if (p) {
        if (_vm_commit(p, 1u << g_lb_bits)) return p;
        (*parent)->free(p);
    }
    return NULL;
}

inline void GlobalAlloc::free(void* p, HugeBlock* hb, uint32 alloc_id) {
    _vm_decommit(p, 1u << g_lb_bits);
    auto& x = _x[alloc_id & (g_array_size - 1)];
    bool r;
    {
        std::lock_guard<std::mutex> g(x.mtx);
        r = hb->free(p) && hb != x.hb;
        if (r) x.lhb.erase(hb);
    }
    if (r) _vm_free(hb, 1u << g_hb_bits);
}

inline LargeBlock* GlobalAlloc::make_large_block(uint32 alloc_id) {
    HugeBlock* parent;
    auto p = this->alloc(alloc_id, &parent);
    return p ? new (p) LargeBlock(parent) : NULL;
}

inline LargeAlloc* GlobalAlloc::make_large_alloc(uint32 alloc_id) {
    HugeBlock* parent;
    auto p = this->alloc(alloc_id, &parent);
    return p ? new (p) LargeAlloc(parent, talloc()) : NULL;
}

inline SmallAlloc* make_small_alloc(LargeBlock* lb, ThreadAlloc* ta) {
    auto p = lb->alloc();
    return p ? new(p) SmallAlloc(lb, ta) : NULL;
}

inline void* ThreadAlloc::alloc(size_t n) {
    void* p = 0;
    SmallAlloc* sa;
    if (n <= 2048) {
        const uint32 u = n > 16 ? god::nb<16>((uint32)n) : 1;
        if (_sa && (p = _sa->alloc(u))) goto end;

        if (_sa && _sa->next) {
            _try_alloc(_lsa, 4, k) {
                if ((p = ((SmallAlloc*)k)->try_hard_alloc(u))) {
                    _lsa.move_front(k);
                    goto end;
                }
            }
        }

        if (_lb && (sa = make_small_alloc(_lb, this))) {
            _lsa.push_front(sa);
            p = sa->alloc(u);
            goto end;
        }

        if (_lb && _lb->next) {
            _try_alloc(_llb, 4, k) {
                if ((sa = make_small_alloc((LargeBlock*)k, this))) {
                    _llb.move_front(k);
                    _lsa.push_front(sa);
                    p = sa->alloc(u);
                    goto end;
                }
            }
        }

        {
            auto lb = _ga->make_large_block(_id);
            if (lb) {
                _llb.push_front(lb);
                sa = make_small_alloc(lb, this);
                _lsa.push_front(sa);
                p = sa->alloc(u);
            }
            goto end;
        }

    } else if (n <= g_max_alloc_size) {
        const uint32 u = god::nb<4096>((uint32)n);
        if (_la && (p = _la->alloc(u))) goto end;

        if (_la && _la->next) {
            _try_alloc(_lla, 4, k) {
                if ((p = ((LargeAlloc*)k)->try_hard_alloc(u))) {
                    _lla.move_front(k);
                    goto end;
                }
            }
        }

        {
            auto la = _ga->make_large_alloc(_id);
            if (la) {
                _lla.push_front(la);
                p = la->alloc(u);
            }
            goto end;
        }

    } else {
        p = ::malloc(n);
    }

end:
    return p;
}

inline void* ThreadAlloc::alloc(size_t n, size_t align) {
    if (align < 32) align = 32;
    assert(align <= 1024 && !(align & (align - 1)));

    void* p = 0;
    SmallAlloc* sa;
    if (n <= 2048) {
        const uint32 a = (uint32)align >> 4;
        const uint32 u = n > 16 ? god::nb<16>((uint32)n) : 1;
        if (_sa && (p = _sa->alloc(u, a))) goto end;

        if (_lb && (sa = make_small_alloc(_lb, this))) {
            _lsa.push_front(sa);
            p = sa->alloc(u, a);
            goto end;
        }

        if (_lb && _lb->next) {
            _try_alloc(_llb, 4, k) {
                if ((sa = make_small_alloc((LargeBlock*)k, this))) {
                    _llb.move_front(k);
                    _lsa.push_front(sa);
                    p = sa->alloc(u, a);
                    goto end;
                }
            }
        }

        {
            auto lb = _ga->make_large_block(_id);
            if (lb) {
                _llb.push_front(lb);
                sa = make_small_alloc(lb, this);
                _lsa.push_front(sa);
                p = sa->alloc(u, a);
            }
            goto end;
        }
    } else {
        p = this->alloc(n);
    }

end:
    return p;
}

inline void ThreadAlloc::free(void* p, size_t n) {
    if (p) {
        if (n <= 2048) {
            const auto sa = (SmallAlloc*) god::align_down<1u << g_sb_bits>(p);
            const auto ta = sa->talloc();
            if (ta == this) {
                if (sa->free(p) && sa != _sa) {
                    _lsa.erase(sa);
                    const auto lb = sa->parent();
                    if (lb->free(sa) && lb != _lb) {
                        _llb.erase(lb);
                        _ga->free(lb, lb->parent(), _id);
                    }
                }
            } else {
                sa->xfree(p);
            }

        } else if (n <= g_max_alloc_size) {
            const auto la = (LargeAlloc*) god::align_down<1u << g_lb_bits>(p);
            const auto ta = la->talloc();
            if (ta == this) {
                if (la->free(p) && la != _la) {
                    _lla.erase(la);
                    _ga->free(la, la->parent(), _id);
                }
            } else {
                la->xfree(p);
            }

        } else {
            ::free(p);
        }
    }
}

inline void* ThreadAlloc::realloc(void* p, size_t o, size_t n) {
    if (unlikely(!p)) return this->alloc(n);
    if (unlikely(o > g_max_alloc_size)) return ::realloc(p, n);
    //if (n <= o) ::abort();

    if (o <= 2048) {
        const uint32 k = (o > 16 ? god::align_up<16>((uint32)o) : 16);
        if (n <= (size_t)k) return p;

        const auto sa = (SmallAlloc*) god::align_down<1u << g_sb_bits>(p);
        if (sa == _sa && n <= 2048) {
            const uint32 l = god::nb<16>((uint32)n);
            auto x = sa->realloc(p, k >> 4, l);
            if (x) return x;
        }

    } else {
        const uint32 k = god::align_up<4096>((uint32)o);
        if (n <= (size_t)k) return p;

        const auto la = (LargeAlloc*) god::align_down<1u << g_lb_bits>(p);
        if (la == _la && n <= g_max_alloc_size) {
            const uint32 l = god::nb<4096>((uint32)n);
            auto x = la->realloc(p, k >> 12, l);
            if (x) return x;
        }
    }

    auto x = this->alloc(n);
    if (x) { memcpy(x, p, o); this->free(p, o); }
    return x;
}

inline void* ThreadAlloc::try_realloc(void* p, size_t o, size_t n) {
    if (unlikely(!p || o > g_max_alloc_size)) return NULL;
    //if (n <= o) ::abort();

    if (o <= 2048) {
        const uint32 k = (o > 16 ? god::align_up<16>((uint32)o) : 16);
        if (n <= (size_t)k) return p;

        const auto sa = (SmallAlloc*) god::align_down<1u << g_sb_bits>(p);
        if (sa == _sa && n <= 2048) {
            const uint32 l = god::nb<16>((uint32)n);
            return sa->realloc(p, k >> 4, l);
        }

    } else {
        const uint32 k = god::align_up<4096>((uint32)o);
        if (n <= (size_t)k) return p;

        const auto la = (LargeAlloc*) god::align_down<1u << g_lb_bits>(p);
        if (la == _la && n <= g_max_alloc_size) {
            const uint32 l = god::nb<4096>((uint32)n);
            return la->realloc(p, k >> 12, l);
        }
    }

    return NULL;
}

void* _salloc(size_t n) {
    assert(n <= 4096);
    return talloc()->salloc(n);
}

void _dealloc(std::function<void()>&& f, int x) {
    g_root->add_destructor(std::forward<destruct_t>(f), x);
}

void* alloc(size_t n) {
    return talloc()->alloc(n);
}

void* alloc(size_t n, size_t align) {
    return talloc()->alloc(n, align);
}

void free(void* p, size_t n) {
    return talloc()->free(p, n);
}

void* realloc(void* p, size_t o, size_t n) {
    return talloc()->realloc(p, o, n);
}

void* try_realloc(void* p, size_t o, size_t n) {
    return talloc()->try_realloc(p, o, n);
}

void* zalloc(size_t size) {
    if (size <= g_max_alloc_size) {
        auto p = co::alloc(size);
        if (p) memset(p, 0, size);
        return p;
    }
    return ::calloc(1, size);
}

char* strdup(const char* s) {
    const size_t n = strlen(s);
    char* const p = (char*) co::alloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

} // co
