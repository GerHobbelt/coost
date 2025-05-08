#include "co/cout.h"

int main(int argc, char** argv) {
    co::cout(co::text("hello\n", color::red));
    co::cout(co::text("hello\n", color::green));
    co::cout(co::text("hello\n", color::yellow));
    co::cout(co::text("hello\n", color::blue));
    co::cout(co::text("hello\n", color::magenta));
    co::cout(co::text("hello\n", color::cyan));
    co::cout(co::text("hello\n", color::bold));

    co::cout("hello ", color::red, "coost ", 23, co::endl, color::deflt);
    return 0;
}
