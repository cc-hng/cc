#pragma once

#include <list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
#include <boost/hana.hpp>
#include <cc/config.h>
#include <cc/util.h>
#include <gsl/gsl>
#include <yyjson.h>

#ifndef CC_WITH_YYJSON
#    error "Recompile with CC_WITH_YYJSON"
#endif

namespace cc {
namespace json {

namespace hana = boost::hana;

struct unknown_t {};
struct null_t {};
static constexpr const null_t null;
using value_t = std::variant<unknown_t, null_t, bool, int, double, std::string>;

namespace detail {

// null
// a boolean
// a string
// a number
// an object (JSON object)
// an array
template <typename T>
struct yyjson_convert;

#define YYJSON_DEFINE_PRIMITIVE_TYPE(type, encode, decode, is_type)     \
    template <>                                                         \
    struct yyjson_convert<type> {                                       \
        static yyjson_mut_val* to_json(yyjson_mut_doc* doc, type rhs) { \
            return encode(doc, rhs);                                    \
        }                                                               \
                                                                        \
        static void from_json(yyjson_val* js, type& rhs) {              \
            ASSERT(is_type(js), "unknown number type");                 \
            rhs = decode(js);                                           \
        }                                                               \
    }

YYJSON_DEFINE_PRIMITIVE_TYPE(bool, yyjson_mut_bool, yyjson_get_bool, yyjson_is_bool);
YYJSON_DEFINE_PRIMITIVE_TYPE(float, yyjson_mut_real, yyjson_get_real, yyjson_is_num);
YYJSON_DEFINE_PRIMITIVE_TYPE(double, yyjson_mut_real, yyjson_get_real, yyjson_is_num);
YYJSON_DEFINE_PRIMITIVE_TYPE(char, yyjson_mut_sint, yyjson_get_sint, yyjson_is_num);
YYJSON_DEFINE_PRIMITIVE_TYPE(int8_t, yyjson_mut_sint, yyjson_get_sint, yyjson_is_num);
YYJSON_DEFINE_PRIMITIVE_TYPE(uint8_t, yyjson_mut_uint, yyjson_get_uint, yyjson_is_num);
YYJSON_DEFINE_PRIMITIVE_TYPE(int16_t, yyjson_mut_sint, yyjson_get_sint, yyjson_is_num);
YYJSON_DEFINE_PRIMITIVE_TYPE(uint16_t, yyjson_mut_uint, yyjson_get_uint, yyjson_is_num);
YYJSON_DEFINE_PRIMITIVE_TYPE(int, yyjson_mut_int, yyjson_get_int, yyjson_is_num);
YYJSON_DEFINE_PRIMITIVE_TYPE(uint32_t, yyjson_mut_uint, yyjson_get_uint, yyjson_is_num);
YYJSON_DEFINE_PRIMITIVE_TYPE(int64_t, yyjson_mut_sint, yyjson_get_sint, yyjson_is_num);
YYJSON_DEFINE_PRIMITIVE_TYPE(uint64_t, yyjson_mut_uint, yyjson_get_uint, yyjson_is_num);

// yyjson_val *
template <>
struct yyjson_convert<yyjson_val*> {
    static yyjson_mut_val* to_json(yyjson_mut_doc* doc, yyjson_val* rhs) {
        return yyjson_val_mut_copy(doc, rhs);
    }

    static void from_json(yyjson_val* js, yyjson_val*& rhs) { rhs = js; }
};

// std::string
template <>
struct yyjson_convert<std::string> {
    static yyjson_mut_val* to_json(yyjson_mut_doc* doc, const std::string& rhs) {
        return yyjson_mut_strncpy(doc, rhs.c_str(), rhs.size());
    }

    static void from_json(yyjson_val* js, std::string& rhs) {
        ASSERT(yyjson_is_str(js), "std::string expect a json string");
        rhs = std::string(yyjson_get_str(js), yyjson_get_len(js));
    }
};

// std::string_view
template <>
struct yyjson_convert<std::string_view> {
    static yyjson_mut_val* to_json(yyjson_mut_doc* doc, std::string_view rhs) {
        return yyjson_mut_strncpy(doc, rhs.data(), rhs.size());
    }

    static void from_json(yyjson_val* js, std::string_view& rhs) {
        ASSERT(yyjson_is_str(js), "std::string_view expect a json string");
        rhs = std::string_view(yyjson_get_str(js), yyjson_get_len(js));
    }
};

// std::vector
template <typename T, typename A>
struct yyjson_convert<std::vector<T, A>> {
    static yyjson_mut_val* to_json(yyjson_mut_doc* doc, const std::vector<T, A>& rhs) {
        auto arr = yyjson_mut_arr(doc);
        for (const auto& v : rhs) {
            auto item = yyjson_convert<T>::to_json(doc, v);
            yyjson_mut_arr_append(arr, item);
        }
        return arr;
    }

    static void from_json(yyjson_val* js, std::vector<T, A>& rhs) {
        ASSERT(yyjson_is_arr(js), "std::vector<T> expect a json array");
        rhs.reserve(yyjson_arr_size(js));
        size_t idx, max;
        yyjson_val* item;
        yyjson_arr_foreach(js, idx, max, item) {
            T v;
            yyjson_convert<T>::from_json(item, v);
            rhs.emplace_back((T&&)v);
        }
    }
};

// std::list
template <typename T, typename A>
struct yyjson_convert<std::list<T, A>> {
    static yyjson_mut_val* to_json(yyjson_mut_doc* doc, const std::list<T, A>& rhs) {
        auto arr = yyjson_mut_arr(doc);
        for (const auto& v : rhs) {
            auto item = yyjson_convert<T>::to_json(doc, v);
            yyjson_mut_arr_append(arr, item);
        }
        return arr;
    }

    static void from_json(yyjson_val* js, std::list<T, A>& rhs) {
        ASSERT(yyjson_is_arr(js), "std::list<T> expect a json array");
        size_t idx, max;
        yyjson_val* item;
        yyjson_arr_foreach(js, idx, max, item) {
            T v;
            yyjson_convert<T>::from_json(item, v);
            rhs.emplace_back((T&&)v);
        }
    }
};

// std::map
template <typename V, typename C, typename A>
struct yyjson_convert<std::map<std::string, V, C, A>> {
    static yyjson_mut_val*
    to_json(yyjson_mut_doc* doc, const std::map<std::string, V, C, A>& rhs) {
        auto obj = yyjson_mut_obj(doc);
        for (const auto& kv : rhs) {
            auto key = yyjson_mut_strncpy(doc, kv.first.c_str(), kv.first.size());
            yyjson_mut_val* val = yyjson_convert<V>::to_json(doc, kv.second);
            yyjson_mut_obj_add(obj, key, val);
        }
        return obj;
    }

    static void from_json(yyjson_val* js, std::map<std::string, V, C, A>& rhs) {
        ASSERT(yyjson_is_obj(js), "std::map<K,V> expect a json object");
        size_t idx, max;
        yyjson_val *key, *val;
        yyjson_obj_foreach(js, idx, max, key, val) {
            std::string k;
            V v;
            yyjson_convert<std::string>::from_json(key, k);
            yyjson_convert<V>::from_json(val, v);
            rhs.emplace((std::string&&)k, (V&&)v);
        }
    }
};

// std::unordered_map
template <typename V, typename C, typename A>
struct yyjson_convert<std::unordered_map<std::string, V, C, A>> {
    static yyjson_mut_val*
    to_json(yyjson_mut_doc* doc, const std::unordered_map<std::string, V, C, A>& rhs) {
        auto obj = yyjson_mut_obj(doc);
        for (const auto& kv : rhs) {
            auto key = yyjson_mut_strncpy(doc, kv.first.c_str(), kv.first.size());
            yyjson_mut_val* val = yyjson_convert<V>::to_json(doc, kv.second);
            yyjson_mut_obj_add(obj, key, val);
        }
        return obj;
    }

    static void from_json(yyjson_val* js, std::unordered_map<std::string, V, C, A>& rhs) {
        ASSERT(yyjson_is_obj(js), "std::unordered_map<K,V> expect a json object");
        size_t idx, max;
        yyjson_val *key, *val;
        yyjson_obj_foreach(js, idx, max, key, val) {
            std::string k;
            V v;
            yyjson_convert<std::string>::from_json(key, k);
            yyjson_convert<V>::from_json(val, v);
            rhs.emplace((std::string&&)k, (V&&)v);
        }
    }
};

// std::optional
template <typename T>
struct yyjson_convert<std::optional<T>> {
    using optional_type = std::optional<T>;

    static yyjson_mut_val* to_json(yyjson_mut_doc* doc, const optional_type& rhs) {
        if (!rhs.has_value()) {
            return nullptr;
        } else {
            return yyjson_convert<T>::to_json(doc, rhs.value());
        }
    }

    static void from_json(yyjson_val* js, optional_type& rhs) {
        if (js == nullptr || yyjson_is_null(js)) {
            rhs = std::nullopt;
        } else {
            T t;
            yyjson_convert<T>::from_json(js, t);
            rhs = t;
        }
    }
};

// json value
template <>
struct yyjson_convert<value_t> {
    static yyjson_mut_val* to_json(yyjson_mut_doc* doc, const value_t& rhs) {
        yyjson_mut_val* val = nullptr;
        std::visit(
            [&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, null_t>) {
                    val = yyjson_mut_null(doc);
                } else if constexpr (!std::is_same_v<T, unknown_t>) {
                    val = yyjson_convert<T>::to_json(doc, v);
                } else {
                    ASSERT(false, "Unknwon json type");
                }
            },
            rhs);
        return val;
    }

    static void from_json(yyjson_val* js, value_t& rhs) {
        if (yyjson_is_bool(js)) {
            rhs = yyjson_get_bool(js);
        } else if (yyjson_is_int(js)) {
            rhs = yyjson_get_int(js);
        } else if (yyjson_is_real(js)) {
            rhs = yyjson_get_real(js);
        } else if (yyjson_is_str(js)) {
            rhs = std::string(yyjson_get_str(js), yyjson_get_len(js));
        } else if (yyjson_is_null(js)) {
            rhs = null;
        } else {
            ASSERT(false, "Unknown json type" + std::to_string(yyjson_get_type(js)));
        }
    }
};

template <typename T>
struct yyjson_convert {
    static_assert(hana::Struct<T>::value, "T expect be a reflection type");

    static yyjson_mut_val* to_json(yyjson_mut_doc* doc, const T& rhs) {
        auto j = yyjson_mut_obj(doc);
        hana::for_each(rhs, [&](const auto& pair) {
            auto key           = hana::to<char const*>(hana::first(pair));
            const auto& member = hana::second(pair);
            using Member = std::remove_const_t<std::remove_reference_t<decltype(member)>>;

            // val == nullptr if Member is a optional
            auto val = yyjson_convert<Member>::to_json(doc, member);
            if (val) {
                yyjson_mut_obj_add_val(doc, j, key, val);
            }
        });
        return j;
    }

    static void from_json(yyjson_val* js, T& rhs) {
        ASSERT(yyjson_is_obj(js), "common T expect a json object");
        hana::for_each(hana::keys(rhs), [&](const auto& key) {
            auto keyname = hana::to<char const*>(key);
            auto& member = hana::at_key(rhs, key);
            using Member = std::remove_reference_t<decltype(member)>;
            try {
                auto val = yyjson_obj_get(js, keyname);
                yyjson_convert<Member>::from_json(val, member);
            } catch (...) {
                ASSERT(false, std::string("Parse key(") + keyname + ") error.");
            }
        });
    }
};

}  // namespace detail

template <typename T>
T convert(yyjson_val* val) {
    T ret;
    detail::yyjson_convert<T>::from_json(val, ret);
    return ret;
}

class JsonParser {
public:
    JsonParser(std::string_view jstr) {
        doc_ = yyjson_read(jstr.data(), jstr.size(), 0);
        ASSERT(doc_, "Parse error");
        root_ = yyjson_doc_get_root(doc_);
    }

    ~JsonParser() {
        if (doc_) {
            yyjson_doc_free(doc_);
            doc_ = nullptr;
        }
    }

    template <typename T>
    T parse() {
        return convert<T>(root_);
    }

private:
    yyjson_doc* doc_;
    yyjson_val* root_;
};

template <typename T>
T parse(std::string_view jstr) {
    JsonParser parser(jstr);
    return parser.parse<T>();
}

template <typename T>
T merge(std::string_view orig, std::string_view tomerge) {
    class Parser {
    public:
        Parser(std::string_view orig, std::string_view tomerge) {
            doc1_ = yyjson_read(orig.data(), orig.size(), 0);
            doc2_ = yyjson_read(tomerge.data(), tomerge.size(), 0);
            ASSERT(doc1_ && doc2_, "Merge parse error");
            merge_doc_ = yyjson_mut_doc_new(NULL);
            auto root  = yyjson_merge_patch(merge_doc_, yyjson_doc_get_root(doc1_),
                                            yyjson_doc_get_root(doc2_));
            yyjson_mut_doc_set_root(merge_doc_, root);
            doc_ = yyjson_mut_doc_imut_copy(merge_doc_, 0);
        }

        ~Parser() {
            static constexpr auto jsfree = [](auto*& doc, auto&& method) {
                if (doc) {
                    method(doc);
                    doc = NULL;
                }
            };
            jsfree(doc_, yyjson_doc_free);
            jsfree(merge_doc_, yyjson_mut_doc_free);
            jsfree(doc1_, yyjson_doc_free);
            jsfree(doc2_, yyjson_doc_free);
        }

        T parse() { return convert<T>(yyjson_doc_get_root(doc_)); }

    private:
        yyjson_mut_doc* merge_doc_;
        yyjson_doc* doc_;
        yyjson_doc* doc1_;
        yyjson_doc* doc2_;
    };

    Parser parser(orig, tomerge);
    return parser.parse();
}

template <typename T>
std::string dump(const T& t) {
    auto doc  = yyjson_mut_doc_new(NULL);
    auto root = detail::yyjson_convert<T>::to_json(doc, t);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_write_err err;
    const char* json_str = yyjson_mut_write_opts(doc, 0, NULL, NULL, &err);
    auto defer           = gsl::finally([&] {
        free((void*)json_str);
        yyjson_mut_doc_free(doc);
    });
    ASSERT(!err.code, err.msg);
    return std::string(json_str);
}

}  // namespace json
}  // namespace cc
