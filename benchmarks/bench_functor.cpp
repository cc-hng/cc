#include "common.h"
#include <any>
#include <functional>
#include <cc/util.h>

inline int add(int a, int b) {
    return a + b;
}

static void bench_func(bench::Bench& b) {
    std::function<int(int, int)> f0 = add;
    std::any a                      = f0;

    std::function<int(int, int)> f2 = add;
    b.title("func");
    b.run("base", [] { bench::doNotOptimizeAway(add(3, 4)); });
    b.run("std::function", [&] { bench::doNotOptimizeAway(f2(3, 4)); });
    b.run("any", [&] {
        const std::function<int(int, int)>* f00 =
            std::any_cast<std::function<int(int, int)>>(&a);
        bench::doNotOptimizeAway((*f00)(3, 4));
    });
}

BENCHMARK_REGISTE(bench_func);
