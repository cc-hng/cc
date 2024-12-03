#pragma once

#include <type_traits>

namespace cc {

namespace detail {

template <typename T>
inline constexpr bool is_noncopyable_v =
    (!std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T>);

}

template <typename T,
          typename = std::enable_if_t<(std::is_constructible_v<T> && detail::is_noncopyable_v<T>)>>
struct SingletonProvider {
    using value_type = T;

    static T& instance() {
        static T ins;
        return ins;
    }
};

}  // namespace cc
