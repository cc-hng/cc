#pragma once

#include <array>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <nameof.hpp>

namespace cc {

namespace detail {
template <typename T>
concept ContainerLike = requires(T t) {
    typename T::value_type;
    { t.begin() } -> std::input_or_output_iterator;
    { t.end() } -> std::input_or_output_iterator;
    { t.size() } -> std::convertible_to<size_t>;
};

template <typename T>
struct type_name {
    static inline std::string name() { return std::string(NAMEOF_TYPE(T)); }
};

template <>
struct type_name<std::string> {
    static inline std::string name() { return "std::string"; }
};

template <>
struct type_name<std::string_view> {
    static inline std::string name() { return "std::string_view"; }
};

template <typename T>
struct is_pair : std::false_type {};
template <typename T1, typename T2>
struct is_pair<std::pair<T1, T2>> : std::true_type {};
template <typename T>
inline constexpr bool is_pair_v = is_pair<T>::value;

// STL associative containers (map/set/multimap/...) expose value_type =
// pair<const K, V>. Atomic constraints short-circuit: when is_pair_v<VT> is
// false the second constraint is never instantiated, so first_type needs no
// existence check. A pair without a const first (e.g. std::vector<pair<K,V>>)
// fails this and recurses into the full pair name instead.
template <typename VT>
concept AssocValue = is_pair_v<VT> && std::is_const_v<typename VT::first_type>;

// std::array also satisfies ContainerLike, so an explicit partial
// specialization is required to keep the N template parameter.
// ponytail: std::span<T, Extent> has the same problem (ContainerLike strips
// the extent); add a specialization here when span names are actually needed.
template <typename T, size_t N>
struct type_name<std::array<T, N>> {
    static inline std::string name() {
        return "std::array<" + type_name<T>::name() + ", " + std::to_string(N) + ">";
    }
};

template <typename T1, typename T2>
struct type_name<std::pair<T1, T2>> {
    static inline std::string name() {
        return "std::pair<" + type_name<std::remove_cv_t<T1>>::name() + ", " + type_name<T2>::name() + ">";
    }
};

template <typename... Args>
struct type_name<std::tuple<Args...>> {
    static inline std::string name() {
        std::string result = "std::tuple<";
        std::string sep = "";
        ((result += sep + type_name<Args>::name(), sep = ", "), ...);
        result += ">";
        return result;
    }
};

template <ContainerLike T>
struct type_name<T> {
    static inline std::string name() {
        // nameof >= 0.10 returns nameof::cstring<N>; bind through a lvalue
        // reference (nameof_type returns const auto&) to convert to string_view.
        std::string_view tn = NAMEOF_TYPE(T);
        auto pos = tn.find('<');
        if (pos == std::string_view::npos) {
            return std::string(tn);
        }

        std::string result = std::string(tn.substr(0, pos));
        result += "<";
        using VT = typename T::value_type;
        if constexpr (AssocValue<VT>) {
            using P1 = std::remove_cv_t<typename VT::first_type>;
            using P2 = typename VT::second_type;
            result += type_name<P1>::name() + ", " + type_name<P2>::name();
        } else {
            result += type_name<VT>::name();
        }
        result += ">";
        return result;
    }
};
}  // namespace detail

}  // namespace cc

// readable but runtime
// variadic like NAMEOF_TYPE: tolerates commas inside template arguments
#define CC_TYPENAME(...) cc::detail::type_name<__VA_ARGS__>::name()
#define CC_TYPENAME_EXPR(...) cc::detail::type_name<decltype(__VA_ARGS__)>::name()
