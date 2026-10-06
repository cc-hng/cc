#pragma once

#include <concepts>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace cc {

namespace detail {

template <typename T>
inline constexpr bool is_noncopyable_v =  //
    (!std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T>);

}

// Explicitly-initialized singleton. Construction runs under std::call_once, so
// init() is idempotent and a throwing constructor is retried on the next call.
// instance() participates in the SAME call_once: a call racing an in-flight
// init() blocks until it completes (no data race, no transient "not inited"
// error), and reads after initialization need no further locking. If
// instance() is called before init() and T is default-constructible it
// auto-initializes; otherwise it throws — the once_flag stays unset on a throw
// (std::call_once semantics), so a later init(args) still takes effect.
template <typename T>
    requires(detail::is_noncopyable_v<T>)
class SingletonProvider {
    static inline std::unique_ptr<T> ins_ = nullptr;
    static inline std::once_flag flag_;

public:
    using value_type = T;

    template <typename... Args>
        requires std::constructible_from<T, Args...>
    static void init(Args&&... args) {
        std::call_once(flag_, [&] { ins_ = std::make_unique<T>(std::forward<Args>(args)...); });
    }

    static T& instance() {
        std::call_once(flag_, [] {
            if constexpr (std::is_default_constructible_v<T>) {
                ins_ = std::make_unique<T>();
            } else {
                throw std::runtime_error("SingletonProvider<T>: call init() before instance()");
            }
        });
        return *ins_;
    }
};

}  // namespace cc
