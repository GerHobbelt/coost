#include "co/cout.h"
#include "co/flag.h"

int main(int argc, char** argv) {
    flag::parse(argc, argv);
    co::cout("hello coost\n");
    return 0;
}
