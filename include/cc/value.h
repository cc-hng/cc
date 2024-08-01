#pragma once

#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cc {

class Value final {
public:
    using array_t  = std::shared_ptr<std::vector<Value>>;
    using object_t = std::shared_ptr<std::unordered_map<std::string, Value>>;
    using variant_t =
        std::variant<std::nullptr_t, bool, int, double, std::string, array_t, object_t>;
    // clang-format off
    Value() : data_(nullptr) {}
    Value(bool v) : data_(v) {}
    Value(int v) : data_(v) {}
    Value(double v) : data_(v) {}
    Value(const char* v) : data_(std::string(v)) {}
    Value(const std::string& v) : data_(v) {}
    Value(array_t v) : data_(v) {}
    Value(object_t v) : data_(v) {}
    Value(const Value& rhs) : data_(rhs.data_) {}
    Value(Value&& rhs) : data_(std::move(rhs.data_)) {}
    Value& operator=(const Value& rhs) { data_ = rhs.data_; return *this; }
    Value& operator=(Value&& rhs) { data_ = std::move(rhs.data_); return *this; }
    // clang-format on

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(data_); }
    bool is_bool() const { return std::holds_alternative<bool>(data_); }
    bool is_int() const { return std::holds_alternative<int>(data_); }
    bool is_double() const { return std::holds_alternative<double>(data_); }
    bool is_string() const { return std::holds_alternative<std::string>(data_); }
    bool is_array() const { return std::holds_alternative<array_t>(data_); }
    bool is_object() const { return std::holds_alternative<object_t>(data_); }

    template <typename T>
    const T& get() const {
        return std::get<T>(data_);
    }

    template <typename T>
    void set(const T& value) {
        data_ = value;
    }

    template <typename T>
    void set(std::string_view key, const T& v) {
        std::istringstream iss(key.data());
        std::string token;
        Value* current = this;

        while (std::getline(iss, token, '.')) {
            if (!current->is_object()) {
                current->data_ = std::make_shared<object_t::element_type>();
            }
            auto& obj = *std::get<object_t>(current->data_);
            current   = &obj[token];
        }
        *current = v;
    }

    template <typename T>
    const T& get(std::string_view key) const {
        std::istringstream iss(key.data());
        std::string token;
        const Value* current = this;

        while (std::getline(iss, token, '.')) {
            if (!current->is_object()) {
                throw std::runtime_error("Invalid path: not an object");
            }
            const auto& obj = *std::get<object_t>(current->data_);
            auto it         = obj.find(token);
            if (it == obj.end()) {
                throw std::runtime_error("Key not found: " + std::string(token));
            }
            current = &it->second;
        }
        return current->get<T>();
    }

    Value& operator[](size_t index) {
        if (!is_array()) {
            data_ = std::make_shared<array_t::element_type>();
        }
        auto& arr = *std::get<array_t>(data_);
        if (index >= arr.size()) {
            arr.resize(index + 1);
        }
        return arr[index];
    }

    Value& operator[](std::string_view key) {
        if (!is_object()) {
            data_ = std::make_shared<object_t::element_type>();
        }
        return (*std::get<object_t>(data_))[std::string(key)];
    }

    size_t size() const {
        if (is_array()) {
            return std::get<array_t>(data_)->size();
        } else if (is_object()) {
            return std::get<object_t>(data_)->size();
        }
        throw std::runtime_error("Cannot get size of non-container value");
    }

    template <typename Fn>
    void visit(Fn&& fn) const {
        std::visit(std::forward<Fn>(fn), data_);
    }

private:
    variant_t data_;
};

}  // namespace cc
