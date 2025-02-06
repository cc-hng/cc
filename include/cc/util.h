#pragma once

#include <stdexcept>
#include <boost/core/demangle.hpp>
#include <boost/core/noncopyable.hpp>
#include <gsl/gsl>

#ifdef __linux__
#    include <pthread.h>
#endif

#define CC_CONCAT0(a, b) a##b
#define CC_CONCAT(a, b)  CC_CONCAT0(a, b)
#define CC_CALL_OUTSIDE(fn) \
    [[maybe_unused]] static const bool CC_CONCAT(__b_, __LINE__) = ((fn), true)

#define ASSERT(cond, msg)                  \
    do {                                   \
        if (GSL_UNLIKELY(!(cond))) {       \
            throw std::runtime_error(msg); \
        }                                  \
    } while (0)

namespace cc {

class NonMutex {
public:
    inline void lock() {}
    inline bool try_lock() { return true; }
    inline void unlock() {}
};

template <typename Mutex>
class LockGuard : boost::noncopyable {
public:
    explicit LockGuard(Mutex& mtx) : mtx_(mtx) { mtx.lock(); }
    ~LockGuard() { mtx_.unlock(); }

private:
    Mutex& mtx_;
};

inline void set_threadname(const char* name) {
#ifdef __linux__
    pthread_setname_np(pthread_self(), name);
#endif
}

template <typename... Args>
std::string type_name() {
    using Signature = void(Args...);
    auto s          = boost::core::demangle(typeid(Signature).name());
    if constexpr (sizeof...(Args) == 1) {
        int len = s.size();
        return s.substr(6, len - 7);
    } else {
        return s.substr(5);
    }
}

}  // namespace cc
