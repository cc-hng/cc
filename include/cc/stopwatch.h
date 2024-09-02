#pragma once

#include <chrono>

// Displays elapsed seconds since construction as double.
namespace cc {

class StopWatch {
public:
    using clock      = std::chrono::steady_clock;
    using time_point = clock::time_point;

    StopWatch() : start_tp_{clock::now()} {}
    explicit StopWatch(time_point tp) : start_tp_((time_point&&)tp) {}
    explicit StopWatch(int ago) : start_tp_(clock::now() - std::chrono::seconds(ago)) {}
    template <typename Duration>
    explicit StopWatch(Duration& duration) : start_tp_(clock::now() - duration) {}

    double elapsed() const {
        return std::chrono::duration<double>(clock::now() - start_tp_).count();
    }

    void reset() { start_tp_ = clock::now(); }

private:
    time_point start_tp_;
};

}  // namespace cc
