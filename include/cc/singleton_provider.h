#pragma once

#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace cc {

namespace detail {

template <typename T>
inline constexpr bool is_noncopyable_v =
    (!std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T>);

}

template <typename T, typename = std::enable_if_t<detail::is_noncopyable_v<T>>>
class SingletonProvider {
    static inline std::unique_ptr<T> ins_ = nullptr;
    static inline std::once_flag flag_;

public:
    using value_type = T;

    template <typename... Args>
    static std::enable_if_t<std::is_constructible_v<T, Args...>>  //
    init(Args&&... args) {
        std::call_once(flag_, [&] { ins_ = std::make_unique<T>(std::forward<Args>(args)...); });
    }

    static T& instance() {
        if constexpr (std::is_constructible_v<T>) {
            init();
        } else {
            if (!ins_) {
                throw std::runtime_error("Expect init first.");
            }
        }
        return *ins_;
    }
};

}  // namespace cc
