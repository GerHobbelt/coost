#pragma once

#include "bitset.h"
#include "co/mem.h"

namespace co {

struct IdGen {
    static const size_t N = 512; // 64 x 8
    static const uint64 I = (uint64)-1;

    struct L1 {
        uint32 c1;
        union {
            Bitset bs;
            uint64 b1;
        };
        Bitset* s; // 64 x uint64
    };

    struct L2 {
        uint32 c2;
        union {
            Bitset bs;
            uint64 b2;
        };
        L1* l1; // 64 x L1
    };

    struct L3 {
        union {
            Bitset bs;
            uint64 b3;
        };
        L2* l2; // 64 x L2
    };

    IdGen() {
        _l3.b3 = 0;
        _l3.l2 = (L2*) co::zalloc(64 * sizeof(L2));
    }

    ~IdGen() {
        for (int i = 0; i < 64; ++i) {
            L2& l2 = _l3.l2[i];
            if (l2.l1) {
                for (int k = 0; k < 64; ++k) {
                    L1& l1 = l2.l1[k];
                    if (l1.s) co::free(l1.s, N);
                }
                co::free(l2.l1, 64 * sizeof(L1));
            }
        }
        co::free(_l3.l2, 64 * sizeof(L2));
    }

    int pop() {
        L1* l1;
        L2* l2;
        int b0, b1, b2, b3;

        b3 = _l3.bs.find_zero();
        if (b3 >= 0) {
            l2 = _l3.l2 + b3;
            if (l2->l1) goto _find_in_l2;

            l2->l1 = (L1*) co::zalloc(64 * sizeof(L1));

        _find_in_l2:
            b2 = l2->bs.find_zero();
            l1 = l2->l1 + b2;
            if (l1->s) goto _find_in_l1;

            ++l2->c2;
            l1->s = (Bitset*) co::zalloc(N);

        _find_in_l1:
            b1 = l1->bs.find_zero();
            b0 = l1->s[b1].find_zero();
            if (l1->s[b1].set_and_fetch(b0) != I) goto _end;
            if (l1->bs.set_and_fetch(b1) != I) goto _end;
            if (l2->bs.set_and_fetch(b2) != I) goto _end;
            _l3.bs.set(b3);

        _end:
            ++l1->c1;
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

        L2& l2 = _l3.l2[b3];
        L1& l1 = l2.l1[b2];
        if (l1.s[b1].fetch_and_unset(b0) != I) goto _xx;
        if (l1.bs.fetch_and_unset(b1) != I) goto _xx;
        if (l2.bs.fetch_and_unset(b2) != I) goto _xx;
        _l3.bs.unset(b3);

    _xx:
        if (--l1.c1 > 0) goto _end;

        co::free(l1.s, N);
        l1.s = nullptr;

        if (--l2.c2 > 0) goto _end;

        co::free(l2.l1, 64 * sizeof(L1));
        l2.l1 = nullptr;

    _end:
        return;
    }

    L3 _l3;
};

} // co
