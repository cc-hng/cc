#pragma once

#include <concepts>
#include <sstream>
#include <stdexcept>
#include <boost/core/noncopyable.hpp>
#include <boost/stacktrace.hpp>

#define CC_CONCAT0(a, b) a##b
#define CC_CONCAT(a, b) CC_CONCAT0(a, b)
#define CC_CALL_OUTSIDE(fn) [[maybe_unused]] static const bool CC_CONCAT(__b_, __LINE__) = ((fn), true)

#define CASSERT(cond)                                       \
    do {                                                    \
        if (!(cond)) [[unlikely]] {                         \
            std::ostringstream oss;                         \
            oss << "\n" << boost::stacktrace::stacktrace(); \
            throw std::runtime_error(oss.str());            \
        }                                                   \
    } while (0)

namespace cc {

template <typename T>
concept MutexLike = requires(T m) {
    { m.lock() } -> std::same_as<void>;
    { m.unlock() } -> std::same_as<void>;
    { m.try_lock() } -> std::convertible_to<bool>;
};

class NonMutex {
public:
    inline void lock() {}
    inline bool try_lock() { return true; }
    inline void unlock() {}
};

template <MutexLike Mutex>
class LockGuard : boost::noncopyable {
    Mutex& mtx_;

public:
    explicit LockGuard(Mutex& mtx) : mtx_(mtx) { mtx.lock(); }
    ~LockGuard() { mtx_.unlock(); }
};

}  // namespace cc
