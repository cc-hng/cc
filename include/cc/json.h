#pragma once

#include <string_view>
#include <type_traits>
#include <cpp_yyjson.hpp>

namespace cc {
namespace json {

template <typename T>
T parse(std::string_view jstr, bool allow_comments = false) {
    yyjson::ReadFlag flag = allow_comments ? yyjson::ReadFlag::AllowComments
                                           : yyjson::ReadFlag::NoFlag;
    // flag                  = flag | yyjson::ReadFlag::ReadInsitu;
    auto rv = yyjson::read(jstr, flag);
    if constexpr (std::is_same_v<yyjson::value, T>) {
        return yyjson::value(rv);
    } else {
        return rv.cast<T>();
    }
}

template <typename T>
std::string dump(T&& t) {
    auto obj = yyjson::value(std::forward<T>(t));
    return std::string(obj.write());
}

}  // namespace json
}  // namespace cc
