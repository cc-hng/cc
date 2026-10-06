#pragma once

#include <functional>
#include <list>
#include <string>
#include <nanobench.h>

namespace bench = ankerl::nanobench;

#define CC_CONCAT0(a, b) a##b
#define CC_CONCAT(a, b) CC_CONCAT0(a, b)
#define CC_CALL_OUTSIDE(fn) [[maybe_unused]] static const bool CC_CONCAT(__b_, __LINE__) = ((fn), true)

class BenchRegistry {
    using BenchFn = std::function<void(bench::Bench&)>;

public:
    static BenchRegistry& get() {
        static BenchRegistry br;
        return br;
    }

    template <typename Fn>
    void registe(std::string what, Fn&& fn, int iters = -1) {
        bench_list_.emplace_back([what, iters, fn = std::forward<Fn>(fn)](bench::Bench& b) {
            b.title(what);
            if (iters > 0) {
                auto old = b.epochIterations();
                b.minEpochIterations(iters);
                fn(b);
                b.minEpochIterations(old);
            } else {
                fn(b);
            }
        });
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

#define BENCHMARK_REGISTE(...) CC_CALL_OUTSIDE(BenchRegistry::get().registe(__VA_ARGS__))
