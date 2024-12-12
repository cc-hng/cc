
#pragma once

#include <charconv>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <cc/type_traits.h>
#include <cc/value.h>
#include <cpp_yyjson.hpp>
#include <gsl/gsl>

// for debug
#include <typeinfo>
#include <boost/core/demangle.hpp>
#include <fmt/core.h>

using var_t        = yyjson::value;
using var_ref      = yyjson::writer::value_ref;
using var_cref     = yyjson::writer::const_value_ref;
using var_obj_t    = yyjson::writer::object;
using var_obj_ref  = yyjson::writer::object_ref;
using var_obj_cref = yyjson::writer::const_object_ref;
using var_arr_t    = yyjson::writer::array;
using var_arr_ref  = yyjson::writer::array_ref;
using var_arr_cref = yyjson::writer::const_array_ref;

namespace cc {

namespace detail {

template <typename T>
T ston(std::string_view s) {
    T ret;
#if defined(_LIBCPP_VERSION)
    if constexpr (std::is_floating_point_v<T>) {
        ret = std::stod(std::string(s));
    } else {
        std::from_chars(s.begin(), s.end(), ret);
    }
#else
    std::from_chars(s.begin(), s.end(), ret);
#endif
    return ret;
}

// clang-format off
inline std::vector<std::string_view>  // clang-format on
str_split(std::string_view raw, std::string_view sep) {
    std::vector<std::string_view> result;
    result.reserve(4);
    boost::algorithm::split(result, raw, boost::is_any_of(std::string(sep)));
    return result;
}

}  // namespace detail

struct var {
    enum type_e { NUL = 0, BOOLEAN, INTEGER, REAL, STRING, ARRAY, OBJECT };

    static var_t from_json(std::string_view s, bool allow_comments = false) {
        yyjson::ReadFlag flag = allow_comments ? yyjson::ReadFlag::AllowComments
                                               : yyjson::ReadFlag::NoFlag;
        return yyjson::value(yyjson::read(s, flag));
    }

    static type_e type(const var_t& self) {
        if (self.is_object()) {
            return type_e::OBJECT;
        } else if (self.is_array()) {
            return type_e::ARRAY;
        } else if (self.is_string()) {
            return type_e::STRING;
        } else if (self.is_real()) {
            return type_e::REAL;
        } else if (self.is_int()) {
            return type_e::INTEGER;
        } else if (self.is_bool()) {
            return type_e::BOOLEAN;
        } else if (self.is_null()) {
            return type_e::NUL;
        }
        GSL_ASSUME(false);
    }

    static var_t clone(const var_t& self) {
        if (self.is_null()) {
            return var_t();
        } else if (self.is_bool()) {
            return *self.as_bool();
        } else if (self.is_int()) {
            return *self.as_int();
        } else if (self.is_real()) {
            return *self.as_real();
        } else if (self.is_string()) {
            return *self.as_string();
        } else {
            return yyjson::read(self.write());  // object | array
        }
    }

    static bool equal(const var_t& lhs, const var_t& rhs) {
        if (lhs.is_null()) {
            return rhs.is_null();
        } else if (lhs.is_bool()) {
            return *lhs.as_bool() == as<bool>(rhs);
        } else if (lhs.is_int()) {
            return *lhs.as_int() == as<std::int64_t>(rhs);
        } else if (lhs.is_real()) {
            return *lhs.as_real() == as<double>(rhs);
        } else if (lhs.is_string()) {
            if (rhs.is_string()) {
                return *lhs.as_string() == *rhs.as_string();
            } else {
                return as<std::string>(lhs) == as<std::string>(rhs);
            }
        } else if (lhs.is_array()) {
            if (!rhs.is_array()) {
                return false;
            }
            auto arr1 = *lhs.as_array();
            auto arr2 = *rhs.as_array();
            int sz    = arr1.size();
            if (sz != arr2.size()) {
                return false;
            }
            for (int i = 0; i < sz; i++) {
                if (!equal(arr1[i], arr2[i])) {
                    return false;
                }
            }
            return true;
        } else if (lhs.is_object()) {
            if (!rhs.is_object()) {
                return false;
            }
            auto obj1 = *lhs.as_object();
            auto obj2 = *rhs.as_object();
            if (obj1.size() != obj2.size()) {
                return false;
            }
            for (const auto [k, v] : obj1) {
                if (!equal(v, obj2[k])) {
                    return false;
                }
            }
            return true;
        } else {
            return lhs.write() == rhs.write();
        }
    }

    static void patch(var_t& self, const var_t& rhs, bool strict = true) {
        if (!(self.is_object() && rhs.is_object())) {
            throw std::runtime_error("var::patch expect object !!!");
        }

        std::vector<std::string_view> toremove;
        toremove.reserve(4);
        auto obj1 = var_obj_ref(self);
        auto obj2 = var_obj_cref(rhs);
        for (auto [k, v] : obj2) {
            if (v.is_null()) {
                toremove.emplace_back(k);
            } else if (obj1.contains(k)) {
                var_t v0 = obj1[k];
                if (strict && type(v0) != type(v)) {
                    throw std::runtime_error("var::patch dismatch type. key=" + std::string(k));
                }

                if (v0.is_object() && v.is_object()) {
                    patch(v0, v, strict);
                } else {
                    v0 = clone(v);
                }
            }
        }

        for (auto k : toremove) {
            obj1.erase(k);
        }
    }

    static void merge(var_t& src, const var_t& dst) {
        if (!(src.is_object() && dst.is_object())) {
            throw std::runtime_error("var::patch expect object !!!");
        }

        std::vector<std::string_view> toremove;
        toremove.reserve(4);
        auto obj1 = *src.as_object();
        auto obj2 = *dst.as_object();
        for (auto [k, v] : obj2) {
            if (v.is_null()) {
                toremove.emplace_back(k);
            } else if (obj1.contains(k)) {
                var_t v0 = obj1[k];
                if (v0.is_object() && v.is_object()) {
                    merge(v0, v);
                } else {
                    v0 = clone(v);
                }
            } else {
                obj1.emplace(k, clone(v));
            }
        }

        for (auto k : toremove) {
            obj1.erase(k);
        }
    }

    static var_ref at(var_t& self, std::string_view ks) {
        auto keys = detail::str_split(ks, ".");
        if (keys.size() == 1) {
            if (GSL_UNLIKELY(!self.is_object())) {
                throw std::runtime_error(fmt::format("var expect object. key={} !!!", ks));
            }
            return (*self.as_object())[ks];
        }

        std::vector<var_ref> parts;
        parts.reserve(keys.size() + 1);

        if (!self.is_object()) {
            self = yyjson::object();
        }
        parts.emplace_back(var_ref(self));
        for (auto k : keys) {
            auto obj = parts.back().as_object();
            if (GSL_UNLIKELY(!obj.has_value())) {
                throw std::runtime_error(fmt::format("var::at({}) expect object. key={} !!!", ks, k));
            }
            if (!(*obj)[k].is_object()) {
                (*obj)[k] = yyjson::object();
            }
            parts.emplace_back((*obj)[k]);
        }
        return parts.back();
    }

    static var_cref at(const var_t& self, std::string_view ks) {
        auto keys = detail::str_split(ks, ".");
        if (keys.size() == 1) {
            if (GSL_UNLIKELY(!self.is_object())) {
                throw std::runtime_error(fmt::format("var expect object. key={} !!!", ks));
            }
            return (*self.as_object())[ks];
        }

        std::vector<var_cref> parts;
        parts.reserve(keys.size() + 1);
        parts.emplace_back(var_cref(self));
        for (auto k : keys) {
            auto obj = parts.back().as_object();
            if (GSL_UNLIKELY(!obj.has_value())) {
                throw std::runtime_error(fmt::format("var::at({}) expect object. key={} !!!", ks, k));
            }
            if (GSL_UNLIKELY(!obj->contains(k))) {
                throw std::runtime_error(
                    fmt::format("var::at({}) doesn't exists. key={} !!!", ks, k));
            }
            parts.emplace_back((*obj)[k]);
        }
        return parts.back();
    }

    template <typename U, typename T = std::remove_cvref_t<U>>
    static T as(const var_t& self) {
        if constexpr (std::is_same_v<T, var_t>) {
            return var::clone(self);
        } else if constexpr (std::is_same_v<T, var_obj_cref>) {
            if (GSL_UNLIKELY(!self.is_object())) {
                throw yyjson::bad_cast("Expect object type. type=" + std::to_string(type(self)));
            }
            return var_obj_cref(self);
        } else if constexpr (std::is_same_v<T, var_arr_cref>) {
            if (GSL_UNLIKELY(!self.is_array())) {
                throw yyjson::bad_cast("Expect array type. type=" + std::to_string(type(self)));
            }
            return var_arr_cref(self);
        } else if constexpr (cc::is_tuple_v<T>) {
            auto arr = var_arr_cref(self);
            if (GSL_UNLIKELY(std::tuple_size_v<T> != arr.size())) {
                throw yyjson::bad_cast(
                    fmt::format("Tuple size mismatch. sizeof(tuple)={}, sizeof(arr)={}, tuple={}",
                                std::tuple_size_v<T>, arr.size(),
                                boost::core::demangle(typeid(T).name())));
            }
            T t;
            std::apply(
                [&arr](auto&... args) {
                    int i = 0;
                    ((args = var::as<std::decay_t<decltype(args)>>(arr[i++])), ...);
                },
                t);
            return {t};
        } else {
            if constexpr (std::is_same_v<T, bool>) {
                if (GSL_UNLIKELY(self.is_string())) {
                    auto s = *self.as_string();
                    return s == "true" || s == "1" || s == "yes" || s == "on" || s == "TRUE"
                           || s == "YES" || s == "ON";
                }
            } else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
                if (GSL_UNLIKELY(self.is_string())) {
                    return detail::ston<T>(*self.as_string());
                }
            } else if constexpr (std::is_same_v<std::string, T>) {
                if (GSL_UNLIKELY(self.is_bool())) {
                    return std::to_string(*self.as_bool());
                } else if (GSL_UNLIKELY(self.is_int())) {
                    return std::to_string(*self.as_int());
                } else if (GSL_UNLIKELY(self.is_real())) {
                    return std::to_string(*self.as_real());
                }
            }
            return self.cast<T>();
        }
    }

    static bool contains(const var_t& self, std::string_view ks) {
        if (!self.is_object()) {
            return false;
        }

        try {
            [[maybe_unused]] auto v = at(self, ks);
            return true;
        } catch (...) {
            return false;
        }
    }

    static void remove(var_t& self, std::string_view ks) {
        if (!self.is_object()) {
            return;
        }

        auto keys = detail::str_split(ks, ".");
        auto last = keys.back();
        keys.pop_back();
        std::vector<var_ref> parts;
        parts.reserve(keys.size() + 1);
        parts.emplace_back(var_ref(self));
        for (auto k0 : keys) {
            auto obj = parts.back().as_object();
            if (GSL_UNLIKELY(!obj.has_value())) {
                return;
            }
            if (!(*obj)[k0].is_object()) {
                return;
            }
            parts.emplace_back((*obj)[k0]);
        }
        auto parent = parts.back();
        if (parent.is_object()) {
            auto o = *parent.as_object();
            o.erase(last);
        }
    }

    template <typename T>
    static T get(const var_t& self, std::string_view ks) {
        auto v = at(self, ks);
        return as<T>(v);
    }

    template <typename T>
    static void set(var_t& self, std::string_view ks, T&& v) {
        at(self, ks) = std::forward<T>(v);
    }
};

}  // namespace cc

using var = cc::var;
