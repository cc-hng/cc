#pragma once

#include <chrono>

// Displays elapsed seconds since construction as double.
namespace cc {

class StopWatch {
    using clock      = std::chrono::steady_clock;
    using time_point = clock::time_point;

    enum { MAX_TIME = 0x7fffffff };
    time_point start_tp_;

public:
    static StopWatch now() { return StopWatch(); }
    static StopWatch min() { return StopWatch(clock::now() - std::chrono::seconds(MAX_TIME)); }
    static StopWatch max() { return StopWatch(clock::now() + std::chrono::seconds(MAX_TIME)); }

    StopWatch() : start_tp_{clock::now()} {}

    inline double elapsed() const {
        return std::chrono::duration<double>(clock::now() - start_tp_).count();
    }

    inline void reset() { start_tp_ = clock::now(); }

private:
    explicit StopWatch(time_point tp) : start_tp_((time_point&&)tp) {}
};

}  // namespace cc
