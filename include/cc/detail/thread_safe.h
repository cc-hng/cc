#pragma once

#include <mutex>
#include <cc/util.h>
#include <gsl/gsl>

namespace cc {

template <typename T,
          MutexLike MutexPolicy = NonMutex,                   //
          typename WriteLock = std::lock_guard<MutexPolicy>,  //
          typename ReadLock = WriteLock>
class ThreadSafeProxy {
    mutable MutexPolicy mtx_;
    T obj_;

    class Guard {
        WriteLock lck_;
        T* p_;

    public:
        Guard(T* p, MutexPolicy& m) : lck_(m), p_(p) {}
        T* operator->() noexcept { return p_; }
        T& operator*() noexcept { return *p_; }
    };

    class ConstGuard {
        ReadLock lck_;
        const T* p_;

    public:
        ConstGuard(const T* p, MutexPolicy& m) : lck_(m), p_(p) {}
        const T* operator->() const noexcept { return p_; }
        const T& operator*() const noexcept { return *p_; }
    };

public:
    template <typename... Args>
    explicit ThreadSafeProxy(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        : obj_(std::forward<Args>(args)...) {}

    ~ThreadSafeProxy() = default;
    ThreadSafeProxy(const ThreadSafeProxy&) = delete;
    ThreadSafeProxy& operator=(const ThreadSafeProxy&) = delete;
    ThreadSafeProxy(ThreadSafeProxy&&) = delete;
    ThreadSafeProxy& operator=(ThreadSafeProxy&&) = delete;

    [[nodiscard]] Guard operator->() { return Guard(&obj_, mtx_); }
    [[nodiscard]] ConstGuard operator->() const { return ConstGuard(&obj_, mtx_); }

    template <typename Func>
        requires std::invocable<Func, T&>
    void withWriteLock(Func&& f) {
        WriteLock lck(mtx_);
        f(obj_);
    }

    template <typename Func>
        requires std::invocable<Func, const T&>
    void withReadLock(Func&& f) {
        ReadLock lck(mtx_);
        f(std::as_const(obj_));
    }
};

}  // namespace cc
