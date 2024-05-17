#pragma once

#define CC_CONCAT0(a, b) a##b
#define CC_CONCAT(a, b)  CC_CONCAT0(a, b)
#define CC_CALL_OUTSIDE(fn) \
    [[maybe_unused]] static const bool CC_CONCAT(__b_, __LINE__) = ((fn), true)

namespace cc {

class NonMutex {
public:
    inline void lock() {}
    inline bool try_lock() { return true; }
    inline void unlock() {}
};

template <typename Mutex>
class LockGuard {
public:
    explicit LockGuard(Mutex& mtx) : mtx_(mtx) { mtx.lock(); }
    ~LockGuard() { mtx_.unlock(); }

private:
    Mutex& mtx_;
};

}  // namespace cc
