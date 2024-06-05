#pragma once

#include <optional>
#include <tuple>
#include <type_traits>
#include <vector>

namespace boost {
namespace asio {
template <typename T, typename Executor>
class awaitable;
}
}  // namespace boost

namespace cc {

template <typename T>
struct is_vector : public std::false_type {};

template <typename T>
struct is_vector<std::vector<T>> : public std::true_type {};

template <typename T>
constexpr bool is_vector_v = is_vector<T>::value;

template <typename T>
struct is_tuple : public std::false_type {};

template <typename... Args>
struct is_tuple<std::tuple<Args...>> : public std::true_type {};

template <typename T>
constexpr bool is_tuple_v = is_tuple<T>::value;

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
#if __cplusplus >= 201703L
    template <typename T, typename Dummy = typename std::invoke_result_t<T, Args...>>
#else
    template <typename T, typename Dummy = typename std::result_of<T(Args...)>::type>
#endif
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

// 定义一个 remove_member_pointer_t 实现
template <typename T>
struct remove_member_pointer;

template <typename Cls, typename R, typename... Args>
struct remove_member_pointer<R (Cls::*)(Args...)> {
    using type = R(Args...);
};

template <typename T>
using remove_member_pointer_t = typename remove_member_pointer<T>::type;

}  // namespace cc
