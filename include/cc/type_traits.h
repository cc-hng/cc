#pragma once

#include <optional>
#include <type_traits>

namespace boost {
namespace asio {
template <typename T, typename Executor>
class awaitable;
}
}  // namespace boost

namespace cc {
/*
 * @brief: Check if a type is std::optional
 * @refer: https://stackoverflow.com/a/62313139
 */
template <typename T>
struct is_optional : public std::false_type {};

template <typename T>
struct is_optional<std::optional<T>> : public std::true_type {};

template <typename T>
constexpr bool is_optional_v = is_optional<T>::value;

// is asio::awaitable<T>
template <typename T>
struct is_awaitable : public std::false_type {};

template <typename T, typename Executor>
struct is_awaitable<boost::asio::awaitable<T, Executor>> : public std::true_type {};

template <typename T>
constexpr bool is_awaitable_v = is_awaitable<T>::value;

// is callable
template <typename F, typename... Args>
struct is_callable {
    // SFINAE  Check
    template <typename T, typename Dummy = typename std::result_of<T(Args...)>::type>
    static constexpr std::true_type check(std::nullptr_t dummy) {
        return std::true_type{};
    };

    template <typename Dummy>
    static constexpr std::false_type check(...) {
        return std::false_type{};
    };

    // the integral_constant's value
    static constexpr bool value = decltype(check<F>(nullptr))::value;
};

template <typename F, typename... Args>
constexpr bool is_callable_v = is_callable<F, Args...>::value;

}  // namespace cc
