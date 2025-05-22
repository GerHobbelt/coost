#include "co/unitest.h"
#include "co/mls.h"

namespace test {

DEF_mls(s, "你好", "hello");

DEF_test(mls) {
    EXPECT_EQ(MLS_s.value(), fastring("你好"));

    co::mls::set_lang_eng();
    EXPECT_EQ(MLS_s.value(), fastring("hello"));

    co::mls::set_lang_chs();
    EXPECT_EQ(MLS_s.value(), fastring("你好"));

    fastring s;
    s << MLS_s;
    EXPECT_EQ(s, "你好");

    s.clear();
    s.cat(MLS_s);
    EXPECT_EQ(s, "你好");
}

} // test
