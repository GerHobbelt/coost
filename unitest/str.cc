#include "co/unitest.h"
#include "co/str.h"

namespace test {

DEF_test(str) {
    DEF_case(split) {
        auto v = str::split("x||y", '|');
        EXPECT_EQ(v.size(), 3);
        EXPECT_EQ(v[0], "x");
        EXPECT_EQ(v[1], "");
        EXPECT_EQ(v[2], "y");

        v = str::split(fastring("x||y"), '|');
        EXPECT_EQ(v.size(), 3);
        EXPECT_EQ(v[0], "x");
        EXPECT_EQ(v[1], "");
        EXPECT_EQ(v[2], "y");

        v = str::split("x||y", '|', 1);
        EXPECT_EQ(v[0], "x");
        EXPECT_EQ(v[1], "|y");

        v = str::split("x y", ' ');
        EXPECT_EQ(v.size(), 2);
        EXPECT_EQ(v[0], "x");
        EXPECT_EQ(v[1], "y");

        v = str::split("\nx\ny\n", '\n');
        EXPECT_EQ(v.size(), 3);
        EXPECT_EQ(v[0], "");
        EXPECT_EQ(v[1], "x");
        EXPECT_EQ(v[2], "y");

        v = str::split(fastring("\nx\ny\n"), '\n');
        EXPECT_EQ(v.size(), 3);
        EXPECT_EQ(v[0], "");
        EXPECT_EQ(v[1], "x");
        EXPECT_EQ(v[2], "y");

        v = str::split("||x||y||", "||");
        EXPECT_EQ(v.size(), 3);
        EXPECT_EQ(v[0], "");
        EXPECT_EQ(v[1], "x");
        EXPECT_EQ(v[2], "y");

        v = str::split("||x||y||", "||", 2);
        EXPECT_EQ(v.size(), 3);
        EXPECT_EQ(v[0], "");
        EXPECT_EQ(v[1], "x");
        EXPECT_EQ(v[2], "y||");

        fastring s("||x||y||");
        v = str::split(s.data(), s.size(), "||", 2, 2);
        EXPECT_EQ(v.size(), 3);
        EXPECT_EQ(v[0], "");
        EXPECT_EQ(v[2], "y||");
    }

    DEF_case(replace) {
        EXPECT_EQ(str::replace("$@xx$@", "$@", "#"), "#xx#");
        EXPECT_EQ(str::replace("$@xx$@", "$@", "#", 1), "#xx$@");
    }

    DEF_case(trim) {
        EXPECT_EQ(str::trim(" \txx\t  \n"), "xx");
        EXPECT_EQ(str::trim("$@xx@", "$@"), "xx");
        EXPECT_EQ(str::trim("$@xx@", "$@", 'l'), "xx@");
        EXPECT_EQ(str::trim("$@xx@", "$@", 'r'), "$@xx");
        EXPECT_EQ(str::trim("xx", ""), "xx");
        EXPECT_EQ(str::trim("", "xx"), "");
        EXPECT_EQ(str::trim("$@xx@", '$'), "@xx@");
        EXPECT_EQ(str::trim("$@xx@", '$', 'r'), "$@xx@");
        EXPECT_EQ(str::trim("$@xx@", '$', 'l'), "@xx@");
        EXPECT_EQ(str::trim("$@xx@", '@'), "$@xx");
        EXPECT_EQ(str::trim("", '\0'), "");

        EXPECT_EQ(str::trim(fastring("$@xx@"), "$@"), "xx");
        EXPECT_EQ(str::trim(fastring("$@xx@"), "$@", 'l'), "xx@");
        EXPECT_EQ(str::trim(fastring("$@xx@"), "$@", 'r'), "$@xx");
        EXPECT_EQ(str::trim(fastring("$@xx@"), '$'), "@xx@");
        EXPECT_EQ(str::trim(fastring("$@xx@"), '$', 'r'), "$@xx@");
        EXPECT_EQ(str::trim(fastring("$@xx@"), '$', 'l'), "@xx@");
        EXPECT_EQ(str::trim(fastring("$@xx@"), '@'), "$@xx");
        EXPECT_EQ(str::trim(fastring("\0xx\0", 4), '\0'), "xx");
    }

    DEF_case(to) {
        EXPECT_EQ(str::to_bool("true"), true);
        EXPECT_EQ(str::to_bool("1"), true);
        EXPECT_EQ(str::to_bool("false"), false);
        EXPECT_EQ(str::to_bool("0"), false);

        EXPECT_EQ(str::to_int32("-32"), -32);
        EXPECT_EQ(str::to_int32("-4k"), -4096);
        EXPECT_EQ(str::to_int64("-64"), -64);
        EXPECT_EQ(str::to_int64("-8G"), -(8LL << 30));

        EXPECT_EQ(str::to_uint32("32"), 32);
        EXPECT_EQ(str::to_uint32("4K"), 4096);
        EXPECT_EQ(str::to_uint64("64"), 64);
        EXPECT_EQ(str::to_uint64("8t"), 8ULL << 40);

        EXPECT_EQ(str::to_double("3.14159"), 3.14159);

        // convertion failed
        int e;
        EXPECT_EQ(str::to_bool("xxx", &e), false);
        EXPECT_EQ(e, EINVAL);

        EXPECT_EQ(str::to_int32("12345678900", &e), 0);
        EXPECT_EQ(e, ERANGE);
        EXPECT_EQ(str::to_int32("-32g, &e"), 0);
        EXPECT_EQ(e, ERANGE);
        EXPECT_EQ(str::to_int32("-3a2", &e), 0);
        EXPECT_EQ(e, EINVAL);

        EXPECT_EQ(str::to_int64("1234567890123456789000", &e), 0);
        EXPECT_EQ(e, ERANGE);
        EXPECT_EQ(str::to_int64("100000P", &e), 0);
        EXPECT_EQ(e, ERANGE);
        EXPECT_EQ(str::to_int64("1di8", &e), 0);
        EXPECT_EQ(e, EINVAL);

        EXPECT_EQ(str::to_uint32("123456789000", &e), 0);
        EXPECT_EQ(e, ERANGE);
        EXPECT_EQ(str::to_uint32("32g", &e), 0);
        EXPECT_EQ(e, ERANGE);
        EXPECT_EQ(str::to_uint32("3g3", &e), 0);
        EXPECT_EQ(e, EINVAL);

        EXPECT_EQ(str::to_uint64("1234567890123456789000", &e), 0);
        EXPECT_EQ(e, ERANGE);
        EXPECT_EQ(str::to_uint64("100000P", &e), 0);
        EXPECT_EQ(e, ERANGE);
        EXPECT_EQ(str::to_uint64("12d8", &e), 0);
        EXPECT_EQ(e, EINVAL);

        EXPECT_EQ(str::to_double("3.141d59", &e), 0);
        EXPECT_EQ(e, EINVAL);

        EXPECT_EQ(str::to_uint32("32", &e), 32);
        EXPECT_EQ(e, 0);
    }

    DEF_case(from) {
        EXPECT_EQ(str::from(3.14), "3.14");
        EXPECT_EQ(str::from(false), "false");
        EXPECT_EQ(str::from(true), "true");
        EXPECT_EQ(str::from(1024), "1024");
        EXPECT_EQ(str::from(-1024), "-1024");
    }
}

} // namespace test
