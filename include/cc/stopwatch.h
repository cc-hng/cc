#pragma once

#include <chrono>

// Displays elapsed seconds since construction as double.
namespace cc {

class StopWatch {
    using clock = std::chrono::steady_clock;
    clock::time_point start_tp_;

public:
    StopWatch() : start_tp_{clock::now()} {}
    StopWatch(int ago) : start_tp_(clock::now() - std::chrono::seconds(ago)) {}
    template <typename Duration>
    StopWatch(Duration& duration) : start_tp_(clock::now() - duration) {}

    double elapsed() const {
        return std::chrono::duration<double>(clock::now() - start_tp_).count();
    }

    void reset() { start_tp_ = clock::now(); }
};

}  // namespace cc
