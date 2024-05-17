#pragma once

#include <functional>
#include <list>
#include <cc/util.h>
#include <nanobench.h>

namespace bench = ankerl::nanobench;

class BenchRegistry {
    using BenchFn = std::function<void(bench::Bench&)>;

public:
    static BenchRegistry& get() {
        static BenchRegistry br;
        return br;
    }

    template <typename Fn>
    void registe(Fn&& fn) {
        bench_list_.emplace_back(std::forward<Fn>(fn));
    }

    void run() {
        for (const auto& f : bench_list_) {
            f(b_);
        }
    }

private:
    BenchRegistry() = default;

private:
    bench::Bench b_;
    std::list<BenchFn> bench_list_;
};

#define BENCHMARK_MAIN              \
    int main() {                    \
        BenchRegistry::get().run(); \
        return 0;                   \
    }

#define BENCHMARK_REGISTE(fn) CC_CALL_OUTSIDE(BenchRegistry::get().registe(fn))
