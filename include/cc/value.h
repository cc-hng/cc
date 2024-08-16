#pragma once

#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>
#include <boost/core/demangle.hpp>
#include <boost/hana.hpp>
#include <cc/type_traits.h>

namespace cc {

namespace hana = boost::hana;

class Value final {
public:
    using string_t  = std::shared_ptr<std::string>;
    using array_t   = std::shared_ptr<std::vector<Value>>;
    using object_t  = std::shared_ptr<std::unordered_map<std::string, Value>>;
    using variant_t = std::variant<std::nullptr_t, bool, int, double, string_t, array_t, object_t>;
    using shared_variant_t = std::shared_ptr<variant_t>;

    // clang-format off
    Value() : data_(std::make_shared<variant_t>(nullptr)) {}
    Value(bool v) : data_(std::make_shared<variant_t>(v)) {}
    Value(int v) : data_(std::make_shared<variant_t>(v)) {}
    Value(double v) : data_(std::make_shared<variant_t>(v)) {}
    Value(const char* v) : data_(std::make_shared<variant_t>(std::make_shared<std::string>(v))) {}
    Value(const std::string& v) : data_(std::make_shared<variant_t>(std::make_shared<std::string>(v))) {}
    Value(array_t v) : data_(std::make_shared<variant_t>(v)) {}
    Value(object_t v) : data_(std::make_shared<variant_t>(v)) {}
    Value(const Value& rhs) : data_(rhs.data_) {}
    Value& operator=(const Value& rhs) { data_ = rhs.data_; return *this; }
    Value(Value&& rhs) : data_(std::move(rhs.data_)) {}
    Value& operator=(Value&& rhs) { data_ = std::move(rhs.data_); return *this; }
    // clang-format on

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(*data_); }
    bool is_bool() const { return std::holds_alternative<bool>(*data_); }
    bool is_int() const { return std::holds_alternative<int>(*data_); }
    bool is_double() const { return std::holds_alternative<double>(*data_); }
    bool is_string() const { return std::holds_alternative<string_t>(*data_); }
    bool is_array() const { return std::holds_alternative<array_t>(*data_); }
    bool is_object() const { return std::holds_alternative<object_t>(*data_); }

    template <typename T>
    T get() const {
        if constexpr (in_variant_v<T, variant_t>) {
            if (std::holds_alternative<T>(*data_)) {
                return std::get<T>(*data_);
            }
        }

        if constexpr (std::is_same_v<T, cc::Value>) {
            return deepcopy(*this);
        } else if constexpr (std::is_same_v<T, bool>) {
            if (is_int()) {
                return std::get<int>(*data_) != 0;
            } else if (is_string()) {
                const auto& str = *std::get<string_t>(*data_);
                return str == "true" || str == "1" || str == "yes" || str == "on";
            }
        } else if constexpr (std::is_integral_v<T>) {
            if (is_double()) {
                return static_cast<T>(std::get<double>(*data_));
            } else if (is_string()) {
                try {
                    const auto& str = *std::get<string_t>(*data_);
                    T result        = std::stoi(str);
                    return result;
                } catch (...) {
                }
            } else {
                return std::get<int>(*data_);
            }
        } else if constexpr (std::is_floating_point_v<T>) {
            if (is_int()) {
                return static_cast<T>(std::get<int>(*data_));
            } else if (is_string()) {
                try {
                    const auto& str = *std::get<string_t>(*data_);
                    T result        = std::stod(str);
                    return result;
                } catch (...) {
                }
            } else {
                return std::get<double>(*data_);
            }
        } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>) {
            if (is_string()) return *std::get<string_t>(*data_);
        } else if constexpr (boost::hana::Struct<T>::value) {
            if (!is_object())
                throw std::runtime_error("Cannot convert non-object Value to struct");
            T ret;
            const auto& obj = *std::get<object_t>(*data_);
            hana::for_each(hana::keys(ret), [&](const auto& key) {
                auto keyname = hana::to<char const*>(key);
                auto& member = hana::at_key(ret, key);
                using Member = std::remove_reference_t<decltype(member)>;
                auto it      = obj.find(keyname);
                if (it != obj.end()) {
                    if constexpr (cc::is_optional<Member>::value) {
                        member = it->second.template get<typename Member::value_type>();
                    } else {
                        member = it->second.template get<Member>();
                    }
                } else if constexpr (!cc::is_optional<Member>::value) {
                    throw std::runtime_error(std::string("Required key not found: ") + keyname);
                }
            });
            return ret;
        } else if constexpr (cc::is_tuple_v<T>) {
            if (!is_array())
                throw std::runtime_error("Cannot convert non-array Value to std::tuple");
            const auto& arr = *std::get<array_t>(*data_);
            if (std::tuple_size_v<T> != arr.size()) {
                throw std::runtime_error("Tuple size mismatch");
            }
            T t;
            std::apply(
                [&arr](auto&... args) {
                    int i = 0;
                    ((args = arr[i++].template get<std::decay_t<decltype(args)>>()), ...);
                },
                t);
            return t;
        } else if constexpr (cc::is_vector_v<T>) {
            if (!is_array())
                throw std::runtime_error("Cannot convert non-array Value to std::vector");
            const auto& arr = *std::get<array_t>(*data_);
            T ret;
            ret.reserve(arr.size());
            for (const auto& elem : arr) {
                ret.push_back(elem.template get<typename T::value_type>());
            }
            return ret;
        } else if constexpr (cc::is_unordered_map_v<T>) {
            if (!is_object())
                throw std::runtime_error("Cannot convert non-object Value to std::unordered_map");
            const auto& obj = *std::get<object_t>(*data_);
            T ret;
            for (const auto& [key, value] : obj) {
                ret.emplace(key, value.template get<typename T::mapped_type>());
            }
            return ret;
        }

        throw std::runtime_error("Unsupported type conversion: "
                                 + boost::core::demangle(typeid(T).name()));
    }

    template <typename T>
    T get(std::string_view key) const {
        std::istringstream iss(key.data());
        std::string token;
        const Value* current = this;

        while (std::getline(iss, token, '.')) {
            if (!current->is_object()) {
                throw std::runtime_error("Cannot access non-object Value by key");
            }
            const auto& obj = *std::get<object_t>(*current->data_);
            auto it         = obj.find(token);
            if (it == obj.end()) {
                throw std::out_of_range("Key not found in object");
            }
            current = &it->second;
        }
        return current->get<T>();
    }

    template <typename T>
    void set(const T& value) {
        if constexpr (in_variant_v<T, variant_t>) {
            *data_ = value;
        } else if constexpr (std::is_same_v<cc::Value, T>) {
            *this = deepcopy(value);
        } else if constexpr (std::is_integral_v<T>) {
            *data_ = (int)value;
        } else if constexpr (std::is_same_v<T, float>) {
            *data_ = (double)value;
        } else if constexpr (std::is_same_v<T, const char*> || std::is_same_v<T, std::string_view>
                             || std::is_same_v<T, std::string>) {
            *data_ = std::make_shared<std::string>(value);
        } else if constexpr (boost::hana::Struct<T>::value) {
            auto obj = std::make_shared<object_t::element_type>();
            hana::for_each(hana::keys(value), [&](const auto& key) {
                auto keyname       = hana::to<char const*>(key);
                const auto& member = hana::at_key(value, key);
                using Member       = std::decay_t<decltype(member)>;
                if constexpr (cc::is_optional<Member>::value) {
                    if (member) {
                        (*obj)[keyname].template set<typename Member::value_type>(*member);
                    }
                } else {
                    (*obj)[keyname].set(member);
                }
            });
            *data_ = obj;
        } else if constexpr (cc::is_tuple_v<T>) {
            auto arr = std::make_shared<array_t::element_type>();
            arr->reserve(std::tuple_size_v<T>);
            std::apply(
                [&](const auto&... xs) {
                    (arr->emplace_back([&](const auto& x) {
                        Value v;
                        v.set<std::decay_t<decltype(x)>>(x);
                        return v;
                    }(xs)),
                     ...);
                },
                value);
            *data_ = std::move(arr);
        } else if constexpr (cc::is_vector_v<T>) {
            auto arr = std::make_shared<array_t::element_type>();
            arr->reserve(value.size());
            for (const auto& elem : value) {
                Value v;
                v.set(elem);
                arr->emplace_back(std::move(v));
            }
            *data_ = std::move(arr);
        } else if constexpr (cc::is_unordered_map_v<T>) {
            auto obj = std::make_shared<object_t::element_type>();
            for (const auto& [key, val] : value) {
                (*obj)[key].set(val);
            }
            *data_ = std::move(obj);
        } else {
            throw std::runtime_error("Unsupported type for set: "
                                     + boost::core::demangle(typeid(T).name()));
        }
    }

    template <int N>
    void set(const char (&value)[N]) {
        int sz = N;
        if (value[sz - 1] == '\0') {
            sz -= 1;
        }
        *data_ = std::make_shared<std::string>(value, sz);
    }

    template <typename T>
    void set(std::string_view key, const T& value) {
        std::istringstream iss(key.data());
        std::string token;
        Value* current = this;

        while (std::getline(iss, token, '.')) {
            if (!current->is_object()) {
                current->set(std::make_shared<object_t::element_type>());
            }
            auto& obj = *std::get<object_t>(*(current->data_));
            current   = &obj[token];
        }
        current->set(value);
    }

    friend bool operator==(const Value& lhs, const Value& rhs) { return *lhs.data_ == *rhs.data_; }

    friend bool operator!=(const Value& lhs, const Value& rhs) { return !(lhs == rhs); }

    Value& operator[](size_t index) {
        if (!is_array()) {
            *data_ = std::make_shared<array_t::element_type>();
        }
        auto& arr = *std::get<array_t>(*data_);
        if (index >= arr.size()) {
            arr.resize(index + 1);
        }
        return arr[index];
    }

    const Value& operator[](size_t index) const {
        if (!is_array()) {
            throw std::runtime_error("Cannot access non-array Value by index");
        }
        const auto& arr = *std::get<array_t>(*data_);
        if (index >= arr.size()) {
            throw std::out_of_range("Array index out of range");
        }
        return arr[index];
    }

    Value& operator[](std::string_view key) {
        if (!is_object()) {
            *data_ = std::make_shared<object_t::element_type>();
        }
        return (*std::get<object_t>(*data_))[std::string(key)];
    }

    const Value& operator[](std::string_view key) const {
        if (!is_object()) {
            throw std::runtime_error("Cannot access non-object Value by key");
        }
        const auto& obj = *std::get<object_t>(*data_);
        auto it         = obj.find(std::string(key));
        if (it == obj.end()) {
            throw std::out_of_range("Key not found in object");
        }
        return it->second;
    }

    size_t size() const {
        if (is_array()) {
            return std::get<array_t>(*data_)->size();
        } else if (is_object()) {
            return std::get<object_t>(*data_)->size();
        }
        throw std::runtime_error("Cannot get size of non-container value");
    }

    template <typename Fn>
    void visit(Fn&& fn) const {
        std::visit(std::forward<Fn>(fn), *data_);
    }

    bool contains(std::string_view key) const {
        if (!is_object()) {
            return false;
        }
        const auto& obj = *std::get<object_t>(*data_);
        return obj.find(std::string(key)) != obj.end();
    }

    void update(const Value& other) {
        if (!is_object() || !other.is_object()) {
            throw std::runtime_error("Both Values must be objects to update");
        }

        auto& obj1 = *std::get<object_t>(*data_);
        auto& obj2 = *std::get<object_t>(*other.data_);
        for (auto& [k, v] : obj1) {
            if (obj2.count(k)) {
                if (v.is_object() && obj2[k].is_object()) {
                    v.update(obj2[k]);
                } else {
                    obj1[k] = deepcopy(obj2[k]);
                }
            }
        }
    }

public:
    static Value deepcopy(const Value& v1) {
        Value result;
        v1.visit([&](auto&& inner) {
            using T = std::decay_t<decltype(inner)>;
            if constexpr (std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, bool>
                          || std::is_same_v<T, int> || std::is_same_v<T, double>) {
                result.set<T>(inner);
            } else if constexpr (std::is_same_v<T, string_t>) {
                result.set<std::string_view>(std::string_view(*inner));
            } else if constexpr (std::is_same_v<T, object_t>) {
                auto obj = std::make_shared<typename object_t::element_type>();
                for (const auto& [k, v] : *inner) {
                    obj->emplace(k, deepcopy(v));
                }
                result.set(obj);
            } else if constexpr (std::is_same_v<T, array_t>) {
                auto arr = std::make_shared<typename array_t::element_type>();
                for (const auto& el : *inner) {
                    arr->emplace_back(deepcopy(el));
                }
                result.set(arr);
            }
        });
        return result;
    }

    // 默认使用v2的值
    static Value merge(const Value& v1, const Value& v2) {
        if (v1.is_object() && v2.is_object()) {
            Value result     = deepcopy(v1);
            const auto& obj2 = *std::get<object_t>(*v2.data_);

            for (const auto& [key, value] : obj2) {
                if (result.contains(key)) {
                    result[key] = merge(result[key], value);
                } else {
                    result[key] = deepcopy(value);
                }
            }
            return result;
        } else if (v1.is_array() && v2.is_array()) {
            Value result     = deepcopy(v1);
            auto& arr1       = *std::get<array_t>(*result.data_);
            const auto& arr2 = *std::get<array_t>(*v2.data_);

            for (const auto& value : arr2) {
                arr1.emplace_back(deepcopy(value));
            }
            return result;
        } else if (v1.is_null()) {
            return deepcopy(v2);
        } else if (v2.is_null()) {
            return deepcopy(v1);
        } else if (v1.data_->index() == v2.data_->index()) {
            // Same types, non-null, non-object, non-array
            return deepcopy(v2);
        } else {
            throw std::runtime_error("Cannot merge Values of different types");
        }
    }

private:
    shared_variant_t data_;
};

}  // namespace cc
