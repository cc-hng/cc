#include "common.h"
#include <cc/functor.h>

inline int add(int a, int b) {
    return a + b;
}

static void bench_func(bench::Bench& b) {
    auto f1                         = cc::bind(add);
    std::function<int(int, int)> f2 = add;
    b.title("func");
    b.run("base", [] { bench::doNotOptimizeAway(add(3, 4)); });
    b.run("functor", [&] { bench::doNotOptimizeAway(cc::invoke(f1, 3, 4)); });
    b.run("std::function", [&] { bench::doNotOptimizeAway(f2(3, 4)); });
}

BENCHMARK_REGISTE(bench_func);
