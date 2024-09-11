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
    enum type_e { NUL = 0, BOOL, INTEGER, REAL, STRING, ARRAY, OBJECT };
    using array_t  = std::vector<Value>;
    using object_t = std::unordered_map<std::string, Value>;
    using variant_t =
        std::variant<std::nullptr_t, bool, int, double, std::string, array_t, object_t>;
    using variant_ptr_t = std::unique_ptr<variant_t>;

    // clang-format off
    Value() : data_(std::make_unique<variant_t>(nullptr)) {}
    Value(bool v) : data_(std::make_unique<variant_t>(v)) {}
    Value(int v) : data_(std::make_unique<variant_t>(v)) {}
    Value(double v) : data_(std::make_unique<variant_t>(v)) {}
    Value(const char* v) : data_(std::make_unique<variant_t>(std::string(v))) {}
    Value(const std::string& v) : data_(std::make_unique<variant_t>(v)) {}
    Value(std::string&& v) : data_(std::make_unique<variant_t>(std::move(v))) {}
    Value(const array_t& v) : data_(std::make_unique<variant_t>(v)) {}
    Value(array_t&& v) : data_(std::make_unique<variant_t>(std::move(v))) {}
    Value(const object_t& v) : data_(std::make_unique<variant_t>(v)) {}
    Value(object_t&& v) : data_(std::make_unique<variant_t>(std::move(v))) {}
    Value(const Value& rhs) : data_(std::make_unique<variant_t>()) { *data_ = *rhs.data_; }
    Value& operator=(const Value& rhs) { *data_ = *rhs.data_; return *this; }
    Value(Value&& rhs) : data_(std::move(rhs.data_)) { }
    Value& operator=(Value&& rhs) { data_ = std::move(rhs.data_); return *this; }
    // clang-format on

    inline int type() const { return data_->index(); }
    inline bool is_null() const { return std::holds_alternative<std::nullptr_t>(*data_); }
    inline bool is_bool() const { return std::holds_alternative<bool>(*data_); }
    inline bool is_integer() const { return std::holds_alternative<int>(*data_); }
    inline bool is_real() const { return std::holds_alternative<double>(*data_); }
    inline bool is_string() const { return std::holds_alternative<std::string>(*data_); }
    inline bool is_array() const { return std::holds_alternative<array_t>(*data_); }
    inline bool is_object() const { return std::holds_alternative<object_t>(*data_); }

    template <typename T>
    std::conditional_t<std::is_same_v<T, array_t> || std::is_same_v<T, object_t>, const T&, T>
    get() const {
        if constexpr (std::is_same_v<T, bool>) {
            return get_bool();
        } else if constexpr (std::is_integral_v<T>) {
            return get_integer();
        } else if constexpr (std::is_floating_point_v<T>) {
            return get_real();
        } else if constexpr (std::is_same_v<T, std::string>) {
            return std::get<std::string>(*data_);
        } else if constexpr (std::is_same_v<T, std::string_view>) {
            return get_string();
        } else if constexpr (std::is_same_v<T, array_t>) {
            return std::get<array_t>(*data_);  // 返回 const 引用
        } else if constexpr (std::is_same_v<T, object_t>) {
            return std::get<object_t>(*data_);  // 返回 const 引用
        } else if constexpr (boost::hana::Struct<T>::value) {
            return get_struct<T>();
        } else if constexpr (cc::is_tuple_v<T>) {
            return get_tuple<T>();
        } else if constexpr (cc::is_vector_v<T>) {
            return get_vector<T>();
        } else if constexpr (cc::is_unordered_map_v<T>) {
            return get_object<T>();
        } else {
            throw std::runtime_error("Unsupported type conversion: "
                                     + boost::core::demangle(typeid(T).name()));
        }
    }

    template <typename T>
    std::conditional_t<std::is_same_v<T, array_t> || std::is_same_v<T, object_t>, const T&, T>
    get(std::string_view key) const {
        std::istringstream iss(key.data());
        std::string token;
        const Value* current = this;

        while (std::getline(iss, token, '.')) {
            if (!current->is_object()) {
                throw std::runtime_error("Cannot access non-object Value by key");
            }
            const auto& obj = std::get<object_t>(*(current->data_));
            auto it         = obj.find(token);
            if (it == obj.end()) {
                throw std::out_of_range("Key not found in object");
            }
            current = &it->second;
        }
        return current->get<T>();
    }

    template <typename T>
    void set(T&& value) {
        using T0 = std::decay_t<T>;

        if constexpr (std::is_same_v<T0, bool>) {
            data_->emplace<bool>(std::forward<T>(value));
        } else if constexpr (std::is_integral_v<T0>) {
            data_->emplace<int>(std::forward<T>(value));
        } else if constexpr (std::is_floating_point_v<T0>) {
            data_->emplace<double>(std::forward<T>(value));
        } else if constexpr (std::is_same_v<T0, const char*> || std::is_same_v<T0, std::string_view>
                             || std::is_same_v<T0, std::string>) {
            data_->emplace<std::string>(std::string(std::forward<T>(value)));
        } else if constexpr (std::is_same_v<T0, array_t>) {
            data_->emplace<array_t>(std::forward<T>(value));
        } else if constexpr (std::is_same_v<T0, object_t>) {
            data_->emplace<object_t>(std::forward<T>(value));
        } else if constexpr (boost::hana::Struct<T0>::value) {
            set_struct(std::forward<T>(value));
        } else if constexpr (cc::is_tuple_v<T0>) {
            set_tuple(std::forward<T>(value));
        } else if constexpr (cc::is_vector_v<T0>) {
            set_vector(std::forward<T>(value));
        } else if constexpr (cc::is_unordered_map_v<T0>) {
            set_object(std::forward<T>(value));
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
        data_->emplace<std::string>(std::string(value, sz));
    }

    template <typename T>
    void set(std::string_view key, T&& value) {
        std::istringstream iss(key.data());
        std::string token;
        Value* current = this;

        while (std::getline(iss, token, '.')) {
            if (!current->is_object()) {
                current->set(object_t{});
            }
            auto& obj = std::get<object_t>(*(current->data_));
            current   = &obj[token];
        }
        current->set(std::forward<T>(value));
    }

    template <typename Fn>
    decltype(auto) visit(Fn&& fn) const {
        return std::visit(std::forward<Fn>(fn), *data_);
    }

    Value& operator[](size_t index) {
        if (!is_array()) {
            set(array_t{});
        }
        auto& arr = std::get<array_t>(*data_);
        if (index >= arr.size()) {
            arr.resize(index + 1);
        }
        return arr[index];
    }

    const Value& operator[](size_t index) const {
        if (!is_array()) {
            throw std::runtime_error("Cannot access non-array Value by index");
        }
        const auto& arr = std::get<array_t>(*data_);
        if (index >= arr.size()) {
            throw std::out_of_range("Array index out of range");
        }
        return arr[index];
    }

    Value& operator[](const std::string& key) {
        if (!is_object()) {
            set(object_t{});
        }
        return (std::get<object_t>(*data_))[key];
    }

    const Value& operator[](const std::string& key) const {
        if (!is_object()) {
            throw std::runtime_error("Cannot access non-object Value by key");
        }
        const auto& obj = std::get<object_t>(*data_);
        auto it         = obj.find(key);
        if (it == obj.end()) {
            throw std::out_of_range("Key not found in object");
        }
        return it->second;
    }

    void remove(std::string_view key) {
        std::istringstream iss(key.data());
        std::string token;
        Value *current = this, *last = this;

        while (std::getline(iss, token, '.')) {
            if (!current->is_object()) {
                return;
            }
            auto& obj = std::get<object_t>(*(current->data_));
            if (obj.find(token) == obj.end()) {
                return;
            }
            last    = current;
            current = &obj[token];
        }

        if (last->is_object()) {
            auto& obj = std::get<object_t>(*(last->data_));
            obj.erase(token);
        }
    }

    inline size_t size() const {
        if (is_array()) {
            return std::get<array_t>(*data_).size();
        } else if (is_object()) {
            return std::get<object_t>(*data_).size();
        }
        throw std::runtime_error("Cannot get size of non-container value");
    }

    inline bool contains(std::string_view key) const {
        std::istringstream iss(key.data());
        std::string token;
        const Value* current = this;

        while (std::getline(iss, token, '.')) {
            if (!current->is_object()) {
                return false;
            }
            const auto& obj = std::get<object_t>(*(current->data_));
            if (obj.find(token) == obj.end()) {
                return false;
            }
            current = &obj.at(token);
        }
        return true;
    }

    void update(const Value& other) {
        if (!is_object() || !other.is_object()) {
            throw std::runtime_error("Both Values must be objects to update");
        }

        auto& obj1 = std::get<object_t>(*data_);
        auto& obj2 = std::get<object_t>(*other.data_);
        for (auto& [k, v] : obj1) {
            if (obj2.count(k)) {
                auto& v2 = obj2.at(k);
                if (v.is_object() && v2.is_object()) {
                    v.update(v2);
                } else {
                    obj1[k] = v2;
                }
            }
        }
    }

    // 默认使用rhs的值
    void merge(const Value& rhs) {
        if (is_object() && rhs.is_object()) {
            const auto& obj2 = rhs.get<object_t>();
            for (const auto& [key, value] : obj2) {
                if (contains(key)) {
                    (*this)[key].merge(value);
                } else {
                    (*this)[key] = value;
                }
            }
        } else if (is_array() && rhs.is_array()) {
            auto& arr1       = std::get<array_t>(*data_);
            const auto& arr2 = std::get<array_t>(*rhs.data_);
            for (const auto& value : arr2) {
                arr1.emplace_back(value);
            }
        } else if (is_null() || (type() == rhs.type())) {
            *this = rhs;
        } else if (rhs.is_null()) {
        } else {
            throw std::runtime_error("Cannot merge Values of different types");
        }
    }

    bool deepequal(const Value& rhs) const {
        try {
            auto t = type();
            switch (t) {
            case NUL: return rhs.is_null();
            case BOOL: return get<bool>() == rhs.get<bool>();
            case INTEGER: return get<int>() == rhs.get<int>();
            case REAL: return get<double>() == rhs.get<double>();
            case STRING: {
                auto s1 = get<std::string_view>();
                auto t2 = rhs.type();
                switch (t2) {
                case BOOL: return s1 == std::to_string(rhs.get_bool());
                case INTEGER: return s1 == std::to_string(rhs.get_integer());
                case REAL: return s1 == std::to_string(rhs.get_real());
                case STRING: return s1 == rhs.get_string();
                default: return false;
                }
            }
            case ARRAY: {
                const auto& a1 = std::get<array_t>(*data_);
                const auto& a2 = std::get<array_t>(*rhs.data_);
                if (a1.size() != a2.size()) {
                    return false;
                }
                int len = a1.size();
                for (int i = 0; i < len; i++) {
                    if (!a1.at(i).deepequal(a2.at(i))) {
                        return false;
                    }
                }
                return true;
            }
            case OBJECT: {
                const auto& o1 = std::get<object_t>(*data_);
                const auto& o2 = std::get<object_t>(*rhs.data_);
                if (o1.size() != o2.size()) {
                    return false;
                }
                for (const auto& [k, v] : o1) {
                    if (o2.find(k) == o2.end() || !v.deepequal(o2.at(k))) {
                        return false;
                    }
                }
                return true;
            }
            default: return false;
            }
        } catch (...) {
            return false;
        }
    }

public:
    static Value merge(const Value& v1, const Value& v2) {
        Value result = v1;
        result.merge(v2);
        return result;
    }

    static bool deepequal(const Value& v1, const Value& v2) { return v1.deepequal(v2); }

private:
    bool get_bool() const {
        auto t = type();
        switch (t) {
        case BOOL: return std::get<bool>(*data_);
        case INTEGER: return std::get<int>(*data_) != 0;
        case STRING: {
            const auto& str = std::get<std::string>(*data_);
            return str == "true" || str == "1" || str == "yes" || str == "on";
        }
        default: throw std::runtime_error(make_error(BOOL));
        }
    }

    int get_integer() const {
        auto t = type();
        switch (t) {
        case INTEGER: return std::get<int>(*data_);
        case BOOL: return static_cast<int>(std::get<bool>(*data_));
        case REAL: return static_cast<int>(std::get<double>(*data_));
        case STRING: {
            const auto& str = std::get<std::string>(*data_);
            return std::stoi(str);
        }
        default: throw std::runtime_error(make_error(INTEGER));
        }
    }

    double get_real() const {
        auto t = type();
        switch (t) {
        case INTEGER: return (double)std::get<int>(*data_);
        case BOOL: return static_cast<double>(std::get<bool>(*data_));
        case REAL: return std::get<double>(*data_);
        case STRING: {
            const auto& str = std::get<std::string>(*data_);
            return std::stod(str);
        }
        default: throw std::runtime_error(make_error(REAL));
        }
    }

    std::string_view get_string() const {
        if (!is_string()) {
            throw std::runtime_error(make_error(STRING));
        }
        const auto& str = std::get<std::string>(*data_);
        return std::string_view(str);
    }

    template <typename T, typename = hana::when<hana::Struct<T>::value>>
    T get_struct() const {
        if (!is_object()) {
            throw std::runtime_error(make_error(OBJECT));
        }
        return T{};
    }

    template <typename T>
    T get_tuple() const {
        if (!is_array()) {
            throw std::runtime_error("Cannot convert non-array Value to std::tuple");
        }
        const auto& arr = std::get<array_t>(*data_);
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
    }

    template <typename T>
    T get_vector() const {
        if (!is_array()) {
            throw std::runtime_error("Cannot convert non-array Value to std::vector");
        }
        const auto& arr = std::get<array_t>(*data_);
        T ret;
        ret.reserve(arr.size());
        for (const auto& elem : arr) {
            ret.push_back(elem.template get<typename T::value_type>());
        }
        return ret;
    }

    template <typename T>
    T get_object() const {
        if (!is_object()) {
            throw std::runtime_error("Cannot convert non-object Value to std::unordered_map");
        }
        const auto& obj = std::get<object_t>(*data_);
        T ret;
        ret.reserve(obj.size());
        for (const auto& [key, value] : obj) {
            ret.emplace(key, value.template get<typename T::mapped_type>());
        }
        return ret;
    }

    template <typename T>
    void set_struct(T&& t) {
        object_t obj;
        hana::for_each(hana::keys(t), [&](const auto& key) {
            auto keyname       = hana::to<char const*>(key);
            const auto& member = hana::at_key(t, key);
            using Member       = std::decay_t<decltype(member)>;
            if constexpr (cc::is_optional<Member>::value) {
                if (member) {
                    obj[keyname].set(*member);
                }
            } else {
                obj[keyname].set(member);
            }
        });
        *data_ = std::move(obj);
    }

    template <typename T>
    void set_tuple(T&& t) {
        array_t arr;
        arr.reserve(std::tuple_size_v<std::decay_t<T>>);
        std::apply(
            [&](const auto&... xs) {
                (arr.emplace_back([&](const auto& x) {
                    Value v;
                    v.set(x);
                    return v;
                }(xs)),
                 ...);
            },
            t);
        *data_ = std::move(arr);
    }

    template <typename T>
    void set_vector(T&& t) {
        array_t arr;
        arr.reserve(t.size());
        for (const auto& elem : t) {
            Value v;
            v.set(elem);
            arr.emplace_back(std::move(v));
        }
        *data_ = std::move(arr);
    }

    template <typename T>
    void set_object(T&& t) {
        object_t obj;
        for (const auto& [key, val] : t) {
            obj[key].set(val);
        }
        *data_ = std::move(obj);
    }

    inline std::string make_error(type_e expect) const {
        return "Unsupported type convert (" + std::to_string(type()) + " -> "
               + std::to_string(expect) + ") !!!";
    }

private:
    variant_ptr_t data_;
};

}  // namespace cc
