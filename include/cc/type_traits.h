#pragma once

#include <optional>
#include <tuple>
#include <type_traits>

#define CC_DEFINE_IS_CONTAINER(container_name, trait_name)          \
    template <typename T>                                           \
    struct trait_name : std::false_type {};                         \
                                                                    \
    template <typename... Args>                                     \
    struct trait_name<container_name<Args...>> : std::true_type {}; \
                                                                    \
    template <typename T>                                           \
    constexpr bool trait_name##_v = trait_name<T>::value;

namespace cc {

/// stl container
CC_DEFINE_IS_CONTAINER(std::tuple, is_tuple)
CC_DEFINE_IS_CONTAINER(std::optional, is_optional)

template <typename T>
struct remove_member_pointer;

template <typename Cls, typename Result, typename... Args>
struct remove_member_pointer<Result (Cls::*)(Args...)> {
    using type = Result(Args...);
};

template <typename Cls, typename Result, typename... Args>
struct remove_member_pointer<Result (Cls::*)(Args...) const> {
    using type = Result(Args...);
};

template <typename Cls, typename Result, typename... Args>
struct remove_member_pointer<Result (Cls::*)(Args...) noexcept> {
    using type = Result(Args...);
};

template <typename Cls, typename Result, typename... Args>
struct remove_member_pointer<Result (Cls::*)(Args...) const noexcept> {
    using type = Result(Args...);
};

template <typename T>
using remove_member_pointer_t = typename remove_member_pointer<T>::type;

// result_of (deprecated — use std::invoke_result_t directly)
// Wraps std::invoke_result_t to accept the legacy Func(Args...) syntax.
template <typename>
struct result_of;

template <typename Func, typename... Args>
struct result_of<Func(Args...)> {
    using type = std::invoke_result_t<Func, Args...>;
};

// DEPRECATED(result_of_t): use std::invoke_result_t<Func, Args...> directly
template <typename T>
using result_of_t = typename result_of<T>::type;

}  // namespace cc
