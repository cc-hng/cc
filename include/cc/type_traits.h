#pragma once

#include <list>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace boost {
namespace asio {
template <typename T, typename Executor>
class awaitable;
}
}  // namespace boost

namespace cc {

template <int i>
using Int2Type = std::integral_constant<int, i>;

/// stl container
template <typename T>
struct is_vector : public std::false_type {};

template <typename T>
struct is_vector<std::vector<T>> : public std::true_type {};

template <typename T>
constexpr bool is_vector_v = is_vector<T>::value;

template <typename T>
struct is_list : public std::false_type {};

template <typename T>
struct is_list<std::list<T>> : public std::true_type {};

template <typename T>
constexpr bool is_list_v = is_list<T>::value;

template <typename T>
struct is_set : public std::false_type {};

template <typename T>
struct is_set<std::set<T>> : public std::true_type {};

template <typename T>
constexpr bool is_set_v = is_set<T>::value;

template <typename T>
struct is_unordered_set : public std::false_type {};

template <typename T, typename A>
struct is_unordered_set<std::unordered_set<T, A>> : public std::true_type {};

template <typename T>
constexpr bool is_unordered_set_v = is_unordered_set<T>::value;

template <typename T>
struct is_map : public std::false_type {};

template <typename T, typename A>
struct is_map<std::map<T, A>> : public std::true_type {};

template <typename T>
constexpr bool is_map_v = is_map<T>::value;

template <typename T>
struct is_unordered_map : public std::false_type {};

template <typename T, typename A>
struct is_unordered_map<std::unordered_map<T, A>> : public std::true_type {};

template <typename T>
constexpr bool is_unordered_map_v = is_unordered_map<T>::value;

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
    template <typename T, typename Dummy = typename std::invoke_result_t<T, Args...>>
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

// result_of
template <typename>
struct result_of;

template <typename F, typename... Args>
struct result_of<F(Args...)> {
    using type = std::invoke_result_t<F, Args...>;
};

template <typename T>
using result_of_t = typename result_of<T>::type;

// in variant
template <typename T, typename Variant>
struct in_variant : std::false_type {};

template <typename T, typename Head, typename... Args>
struct in_variant<T, std::variant<Head, Args...>>
  : std::conditional_t<std::is_same_v<T, Head>, std::true_type, in_variant<T, std::variant<Args...>>> {
};

template <typename T, typename Variant>
constexpr bool in_variant_v = in_variant<T, Variant>::value;

}  // namespace cc
