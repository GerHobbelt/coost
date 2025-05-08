#include "co/unitest.h"
#include "co/mem.h"
#include "../src/mem.h"

namespace test {

DEF_test(mem) {
    DEF_case(bitops) {
        EXPECT_EQ(_find_lsb(1), 0);
        EXPECT_EQ(_find_lsb(12), 2);
        EXPECT_EQ(_find_lsb(3u << 20), 20);
    #if __arch64
        EXPECT_EQ(_find_msb(1), 0);
        EXPECT_EQ(_find_msb(12), 3);
        EXPECT_EQ(_find_msb(3u << 20), 21);
        EXPECT_EQ(_find_msb(1ull << 63), 63);
        EXPECT_EQ(_find_msb(~0ull), 63);
        EXPECT_EQ(_find_lsb(~0ull), 0);
    #else
        EXPECT_EQ(_find_msb(1), 0);
        EXPECT_EQ(_find_msb(12), 3);
        EXPECT_EQ(_find_msb(3u << 20), 21);
        EXPECT_EQ(_find_msb(1ull << 31), 31);
        EXPECT_EQ(_find_msb(~0u), 31);
        EXPECT_EQ(_find_lsb(~0u), 0);
    #endif

        EXPECT_EQ(_pow2_align(2), 2);
        EXPECT_EQ(_pow2_align(3), 4);
        EXPECT_EQ(_pow2_align(5), 8);
        EXPECT_EQ(_pow2_align(7), 8);
        EXPECT_EQ(_pow2_align(8), 8);
        EXPECT_EQ(_pow2_align(58), 64);
        EXPECT_EQ(_pow2_align(63), 64);
        EXPECT_EQ(_pow2_align(65), 128);
    }

    DEF_case(static_mem) {
        co::StaticMem m(8 * 1024);
        char* p = (char*) m.alloc(15);
        EXPECT(p == m._h->p);
        EXPECT_EQ(m._pos, 16);

        char* q = (char*) m.alloc(63, 64);
        EXPECT_EQ((size_t)q & 63, 0);
        EXPECT_EQ(m._pos, god::cast<uint32>(q - p) + 64);

        char* r = (char*) m.alloc(63, 64);
        EXPECT(((size_t)r & 63) == 0);
        EXPECT(r == q + 64);

        q = (char*) m.alloc(31);
        EXPECT(q == r + 64);

        r = (char*) m.alloc(63, 64);
        EXPECT(r == q + 64);

        uint32 x = god::cast<uint32>(r - p) + 64;
        EXPECT_EQ(x, m._pos);

        uint32 k = m._blk_size - x - sizeof(co::Memb);
        q = (char*) m.alloc(k);
        EXPECT(q == r + 64);
        EXPECT((char*)m._h + m._blk_size == q + k);
        EXPECT(m._h && !m._h->next);

        q = (char*) m.alloc(63, 64);
        EXPECT(m._h && m._h->next && !m._h->next->next);
        EXPECT(q < p || q >= (p + m._blk_size - sizeof(co::Memb)));
    }

    DEF_case(dealloc) {
        int v = 0;
        {
            co::Dealloc da;
            da.add_destructor([&v]() { v = 3; });
            da.add_destructor([&v]() { v = 8; });
            EXPECT(da._m._h && !da._m._h->next);

            for (int i = 0; i < 8192 / sizeof(co::destruct_t); ++i) {
                da.add_destructor([&v]() { v = 7; });
            }
            EXPECT(da._m._h && da._m._h->next && !da._m._h->next->next);
        }
        EXPECT_EQ(v, 3)
    }

    DEF_case(small) {
        void* p = co::alloc(2048);
        EXPECT_NE(p, (void*)0);
        *(uint32*)p = 7;
        EXPECT_EQ(*(uint32*)p, 7);
        co::free(p, 2048);

        void* x = p;
        p = co::alloc(8);
        EXPECT_EQ(p, x);
        co::free(p, 8);

        p = co::alloc(72);
        EXPECT_EQ(p, x);
        co::free(p, 72);

        p = co::alloc(4096);
        EXPECT_NE(p, (void*)0);
        EXPECT_NE(p, x);
        co::free(p, 4096);

        p = co::alloc(15, 32);
        EXPECT(((size_t)p & 31) == 0);

        void* a = co::alloc(31, 32);
        EXPECT_EQ((size_t)a - (size_t)p, 32);
        co::free(a, 31);

        void* b = co::alloc(31, 64);
        EXPECT(((size_t)b & 63) == 0);
        EXPECT_EQ(god::align_up((size_t)p + 32, 64), (size_t)b);

        void* c = co::alloc(223, 128);
        EXPECT(((size_t)c & 127) == 0);

        void* d = co::alloc(31, 1024);
        EXPECT(((size_t)d & 1023) == 0);

        co::free(d, 31);
        co::free(c, 223);
        co::free(b, 31);
        co::free(p, 15);

        int* v = co::_new<int>(7);
        EXPECT_EQ(*v, 7);
        co::_delete(v);
    }

    DEF_case(realloc) {
        void* p;
        p = co::alloc(48);
        EXPECT_NE(p, (void*)0);
        *(uint32*)p = 7;
        EXPECT_EQ(*(uint32*)p, 7);

        void* x = p;
        p = co::realloc(p, 48, 64);
        EXPECT_EQ(p, x);
        EXPECT_EQ(*(uint32*)p, 7);

        p = co::try_realloc(p, 64, 128);
        EXPECT_EQ(p, x);

        p = co::realloc(p, 128, 2048);
        EXPECT_EQ(p, x);

        p = co::realloc(p, 2048, 4096);
        EXPECT_NE(p, x);
        EXPECT_EQ(*(uint32*)p, 7);

        x = p;
        p = co::realloc(p, 4096, 8 * 1024);
        EXPECT_EQ(p, x);
        EXPECT_EQ(*(uint32*)p, 7);

        p = co::realloc(p, 8 *1024, 32 * 1024);
        EXPECT_EQ(p, x);

        p = co::realloc(p, 32 * 1024, 64 * 1024);
        EXPECT_EQ(p, x);

        x = p;
        p = co::realloc(p, 64 * 1024, 132 * 1024);
        EXPECT_NE(p, x);
        EXPECT_EQ(*(uint32*)p, 7);

        p = co::realloc(p, 132 * 1024, 256 * 1024);
        EXPECT_EQ(*(uint32*)p, 7);
        co::free(p, 256 * 1024);
    }

    DEF_case(static) {
        int* x = co::make_static<int>(7);
        EXPECT_NE(x, (void*)0);
        EXPECT_EQ(*x, 7);

        int* r = co::make_rootic<int>(7);
        EXPECT_EQ(*r, 7);
    }

    static int gc = 0;
    static int gd = 0;

    struct A {
        A() {}
        virtual ~A() {}
    };

    struct B : A {
        B() { ++gc; }
        virtual ~B() { ++gd; }
    };

    DEF_case(unique) {
        co::unique<int> p;
        EXPECT(p == NULL);
        EXPECT(!p);

        p = co::make_unique<int>(7);
        EXPECT_EQ(*p, 7);
        *p = 3;
        EXPECT_EQ(*p, 3);

        auto q = co::make_unique<int>(7);
        EXPECT_EQ(*q, 7);

        q = std::move(p);
        EXPECT_EQ(*q, 3);
        EXPECT(p == NULL);

        p = q;
        EXPECT_EQ(*p, 3);
        EXPECT(q == NULL);

        p.swap(q);
        EXPECT_EQ(*q, 3);
        EXPECT(p == NULL);

        q.reset();
        EXPECT(q == NULL);

        co::unique<A> a = co::make_unique<B>();
        EXPECT_EQ(gc, 1);
        a.reset();
        EXPECT_EQ(gd, 1);
    }

    DEF_case(shared) {
        co::shared<int> p;
        EXPECT(p == NULL);
        EXPECT(!p);
        EXPECT_EQ(p.ref_count(), 0);

        co::shared<int> q(p);
        EXPECT_EQ(p.ref_count(), 0);
        EXPECT_EQ(q.ref_count(), 0);

        p = co::make_shared<int>(7);
        EXPECT_EQ(*p, 7);
        *p = 3;
        EXPECT_EQ(*p, 3);
        EXPECT_EQ(p.ref_count(), 1);
        EXPECT_EQ(q.ref_count(), 0);

        q = p;
        EXPECT_EQ(p.ref_count(), 2);
        EXPECT_EQ(q.ref_count(), 2);
        EXPECT_EQ(*q, 3);

        p.reset();
        EXPECT(p == NULL);
        EXPECT_EQ(q.ref_count(), 1);
        EXPECT_EQ(*q, 3);

        p.swap(q);
        EXPECT(q == NULL);
        EXPECT_EQ(p.ref_count(), 1);
        EXPECT_EQ(*p, 3);

        q = std::move(p);
        EXPECT(p == NULL);
        EXPECT_EQ(q.ref_count(), 1);
        EXPECT_EQ(*q, 3);

        co::shared<A> a = co::make_shared<B>();
        EXPECT_EQ(gc, 2);

        auto b = a;
        EXPECT_EQ(gc, 2);
        EXPECT_EQ(a.ref_count(), 2);

        b.reset();
        EXPECT_EQ(gd, 1);
        EXPECT_EQ(a.ref_count(), 1);

        a.reset();
        EXPECT_EQ(a.ref_count(), 0);
        EXPECT_EQ(gd, 2);
    }
}

} // namespace test
