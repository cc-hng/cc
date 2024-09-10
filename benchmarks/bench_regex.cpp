
#include "common.h"
#include <regex>
#include <re2/re2.h>

static void bench_regex(bench::Bench& b) {
    std::string str = "/api/service/method";
    b.title("regex");
    b.run("base", [&] {
        bool b = str == "/api/service/method";
        bench::doNotOptimizeAway(b);
    });
    std::regex re1("/api/service/method");
    b.run("std::regex", [&] {
        std::smatch base_match;
        auto b = std::regex_match(str, base_match, re1);
        bench::doNotOptimizeAway(b);
    });

    RE2 re2("/api/service/method");
    b.run("re2::re2", [&] {
        auto b = RE2::FullMatch(str, re2);
        bench::doNotOptimizeAway(b);
    });
}

BENCHMARK_REGISTE(bench_regex);
