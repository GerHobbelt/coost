#include "co/co.h"
#include "co/cout.h"
#include "co/flag.h"

// 让 main 函数中的代码运行在协程中

int _main(int argc, char** argv); 

int main(int argc, char** argv) {
    flag::parse(argc, argv);
    int r;
    co::wait_group wg(1);
    go([&](){
        r = _main(argc, argv);
        wg.done();
    });
    wg.wait();
    return r;
}

int _main(int argc, char** argv) {
    co::cout("hello co\n");
    return 0;
}
