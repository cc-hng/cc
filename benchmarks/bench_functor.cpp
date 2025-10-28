#include "common.h"
#include <any>
#include <functional>
#include <boost/compat/function_ref.hpp>
#include <cc/util.h>
#include <cc/value.h>

int add(int a, int b) {
    return a + b;
}

static void bench_func(bench::Bench& b) {
    std::function<int(int, int)> f0 = add;
    std::any a                      = f0;

    int (*f1)(int, int)                           = add;
    std::function<int(int, int)> f2               = add;
    boost::compat::function_ref<int(int, int)> f3 = add;
    b.title("func");
    b.run("base", [] { bench::doNotOptimizeAway(add(3, 4)); });
    b.run("func *", [&] { bench::doNotOptimizeAway(f1(3, 4)); });
    b.run("std::function", [&] { bench::doNotOptimizeAway(f2(3, 4)); });
    b.run("boost::function_ref", [&] { bench::doNotOptimizeAway(f3(3, 4)); });
    b.run("any", [&] {
        const std::function<int(int, int)>* f00 = std::any_cast<std::function<int(int, int)>>(&a);
        bench::doNotOptimizeAway((*f00)(3, 4));
    });
    b.run("call_as_tuple", [] { bench::doNotOptimizeAway(std::apply(add, std::make_tuple(3, 4))); });
    var_arr_t arr;
    arr.emplace_back(3);
    arr.emplace_back(5);
    b.run("call_as_var",
          [&] { bench::doNotOptimizeAway(std::apply(add, arr.cast<std::tuple<int, int>>())); });
}

BENCHMARK_REGISTE(bench_func);
