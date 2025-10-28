#pragma once

#include <list>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#define CC_HAS_MEMBER(member)                                                                 \
    template <typename T, typename... Args>                                                   \
    struct has_member_##member {                                                              \
    private:                                                                                  \
        template <typename U>                                                                 \
        static auto Check(int)                                                                \
            -> decltype(std::declval<U>().member(std::declval<Args>()...), std::true_type()); \
        template <typename U>                                                                 \
        static std::false_type Check(...);                                                    \
                                                                                              \
    public:                                                                                   \
        enum { value = std::is_same<decltype(Check<T>(0)), std::true_type>::value };          \
    };                                                                                        \
                                                                                              \
    template <typename F, typename... Args>                                                   \
    constexpr bool has_member_##member##_v = has_member_##member<F, Args...>::value;

#define CC_DEFINE_IS_CONTAINER(contianer_name, trait_name)          \
    template <typename T>                                           \
    struct trait_name : std::false_type {};                         \
                                                                    \
    template <typename... Args>                                     \
    struct trait_name<contianer_name<Args...>> : std::true_type {}; \
                                                                    \
    template <typename T>                                           \
    constexpr bool trait_name##_v = trait_name<T>::value;

#ifdef CC_ENABLE_COROUTINE
namespace boost {
namespace asio {
template <typename T, typename Executor>
class awaitable;
}
}  // namespace boost
#endif

namespace cc {

template <int i>
using Int2Type = std::integral_constant<int, i>;

/// stl container
CC_DEFINE_IS_CONTAINER(std::vector, is_vector)
CC_DEFINE_IS_CONTAINER(std::list, is_list)
CC_DEFINE_IS_CONTAINER(std::set, is_set)
CC_DEFINE_IS_CONTAINER(std::unordered_set, is_unordered_set)
CC_DEFINE_IS_CONTAINER(std::map, is_map)
CC_DEFINE_IS_CONTAINER(std::unordered_map, is_unordered_map)
CC_DEFINE_IS_CONTAINER(std::tuple, is_tuple)
CC_DEFINE_IS_CONTAINER(std::optional, is_optional)
CC_DEFINE_IS_CONTAINER(std::shared_ptr, is_shared_ptr)
CC_DEFINE_IS_CONTAINER(std::weak_ptr, is_weak_ptr)


#ifdef CC_ENABLE_COROUTINE

CC_DEFINE_IS_CONTAINER(boost::asio::awaitable, is_awaitable)

#else

template <typename T>
constexpr bool is_awaitable_v = false;

#endif

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

// has_member
CC_HAS_MEMBER(get)

}  // namespace cc
