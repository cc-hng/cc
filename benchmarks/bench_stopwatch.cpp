#include "common.h"
#include <absl/time/clock.h>
#include <absl/time/time.h>
#include <cc/stopwatch.h>
#include <time.h>

#if defined(__linux__)
#    include <sys/time.h>
#endif

static void bench_stopwatch(bench::Bench& b) {
    cc::StopWatch stopwatch;
    b.title("Stopwatch");
    auto old = b.epochIterations();
    b.minEpochIterations(522527);
    b.run("ctor", [] {
        cc::StopWatch stopwatch;
        bench::doNotOptimizeAway(stopwatch);
    });
    b.run("elapsed", [&] {
        auto e = stopwatch.elapsed();
        bench::doNotOptimizeAway(e);
    });
    b.run("time(null)", [] {
        auto now = time(NULL);
        bench::doNotOptimizeAway(now);
    });
#if defined(__linux__)
    b.run("gettimeofday", [] {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        bench::doNotOptimizeAway(tv);
    });
#endif
    b.run("absl::time", [] {
        auto now = absl::GetCurrentTimeNanos();
        bench::doNotOptimizeAway(now);
    });
    b.minEpochIterations(old);
}

BENCHMARK_REGISTE(bench_stopwatch);
