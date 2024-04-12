#include "common.h"
#include <cc/stopwatch.h>

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
    b.minEpochIterations(old);
}

BENCHMARK_REGISTE(bench_stopwatch);
