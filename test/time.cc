#include "co/time.h"
#include "co/benchmark.h"
#include <time.h>
#ifndef _WIN32
#include <sys/time.h>
#endif

// speed testing
// mac: 
//   gettimeofday > now::ms(), now::us() > clock_gettime > time(0) > now::str()
// linux:
//   time(0) > gettimeofday, now::ms(), now::us(), clock_gettime > now::str()

BM_group(time) {
    int64 v;
    fastring s;
    BM_add(now::str()) {
        s = time::str("%Y");
    }
    BM_use(s);

   // on linux: time(0) is fast, on mac: time(0) is slow
    BM_add(time(0)) {
        v = ::time(0);
    }
    BM_use(v);

    BM_add(time::mono.us()) {
        v = time::mono.us();
    }
    BM_use(v);

    BM_add(time::mono.ms()) {
        v = time::mono.ms();
    }
    BM_use(v);

#ifndef _WIN32
    struct timeval tv;
    BM_add(gettimeofday) {
        gettimeofday(&tv, 0);
    }
    BM_use(tv);

    struct timespec ts;
    BM_add(clock_gettime) {
      clock_gettime(CLOCK_MONOTONIC, &ts);
    }
    BM_use(ts);
#endif
}

int main(int argc, char** argv) {
    co::run_benchmarks();
    return 0;
}
