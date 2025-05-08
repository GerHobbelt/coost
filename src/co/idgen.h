#pragma once

#include "bitops.h"
#include "co/mem.h"

namespace co {

struct IdGen {
    static const size_t N = 512; // 64 x 8
    static const uint64 I = (uint64)-1;

    struct L1 {
        uint32 used;
        uint64 b;
        uint64* p; // 64 x uint64
    };

    struct L2 {
        uint32 used;
        uint64 b;
        L1* p; // 64 x L1
    };

    struct L3 {
        uint64 b;
        L2* p; // 64 x L2
    };

    IdGen() {
        _l3.b = 0;
        _l3.p = (L2*) co::zalloc(64 * sizeof(L2));
    }

    ~IdGen() {
        for (int i = 0; i < 64; ++i) {
            L2& l2 = _l3.p[i];
            if (l2.p) {
                for (int k = 0; k < 64; ++k) {
                    L1& l1 = l2.p[k];
                    if (l1.p) co::free(l1.p, N);
                }
                co::free(l2.p, 64 * sizeof(L1));
            }
        }
        co::free(_l3.p, 64 * sizeof(L2));
    }

    int pop() {
        L1* l1;
        L2* l2;
        int b0, b1, b2, b3;

        b3 = bit::find_0(_l3.b);
        if (b3 >= 0) {
            l2 = _l3.p + b3;
            if (l2->p) goto _find_in_l2;

            l2->p = (L1*) co::zalloc(64 * sizeof(L1));

        _find_in_l2:
            b2 = _find_lsb(~l2->b);
            l1 = l2->p + b2;
            if (l1->p) goto _find_in_l1;

            ++l2->used;
            l1->p = (uint64*) co::zalloc(N);

        _find_in_l1:
            b1 = bit::find_0(l1->b);
            b0 = bit::find_0(l1->p[b1]);
            if (bit::set_and_fetch(l1->p[b1], b0) != I) goto _end;
            if (bit::set_and_fetch(l1->b, b1) != I) goto _end;
            if (bit::set_and_fetch(l2->b, b2) != I) goto _end;
            bit::set(_l3.b, b3);

        _end:
            ++l1->used;
            return (b3 << 18) + (b2 << 12) + (b1 << 6) + b0;
        }

        ::abort();
        return -1;
    }

    void push(int id) {
        const int b3 = id >> 18;
        const int r3 = id & ((1 << 18) - 1);
        const int b2 = r3 >> 12;
        const int r2 = r3 & ((1 << 12) - 1);
        const int b1 = r2 >> 6;
        const int b0 = r2 & 63;

        L2& l2 = _l3.p[b3];
        L1& l1 = l2.p[b2];
        if (bit::fetch_and_unset(l1.p[b1], b0) != I) goto _xx;
        if (bit::fetch_and_unset(l1.b, b1) != I) goto _xx;
        if (bit::fetch_and_unset(l2.b, b2) != I) goto _xx;
        bit::unset(_l3.b, b3);

    _xx:
        if (--l1.used > 0) goto _end;

        co::free(l1.p, N);
        l1.p = nullptr;

        if (--l2.used > 0) goto _end;

        co::free(l2.p, 64 * sizeof(L1));
        l2.p = nullptr;

    _end:
        return;
    }

    L3 _l3;
};

} // co
