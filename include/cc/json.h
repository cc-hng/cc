#pragma once

#ifndef CC_WITH_YYJSON
#    error "Recompile with CC_WITH_YYJSON"
#endif

#include <list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <boost/hana.hpp>
#include <cc/util.h>
#include <cc/value.h>
#include <gsl/gsl>
#include <yyjson.h>

namespace cc {
namespace json {

namespace hana = boost::hana;

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

// std::tuple
template <typename... Args>
struct yyjson_convert<std::tuple<Args...>> {
    static yyjson_mut_val* to_json(yyjson_mut_doc* doc, const std::tuple<Args...>& rhs) {
        auto arr = yyjson_mut_arr(doc);
        std::apply(
            [&](const auto&... xs) {
                ((yyjson_mut_arr_append(
                     arr, yyjson_convert<std::decay_t<decltype(xs)>>::to_json(doc, xs))),
                 ...);
            },
            rhs);
        return arr;
    }

    static void from_json(yyjson_val* js, std::tuple<Args...>& rhs) {
        ASSERT(yyjson_is_arr(js), "std::tuple<Args...> expect a json array");
        ASSERT(yyjson_arr_size(js) == sizeof...(Args), "std::tuple<Args...> count error");
        std::apply(
            [&](auto&... as) {
                int i = 0;
                ((yyjson_convert<std::decay_t<decltype(as)>>::from_json(yyjson_arr_get(js, i++), as)),
                 ...);
            },
            rhs);
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
    static yyjson_mut_val* to_json(yyjson_mut_doc* doc, const std::map<std::string, V, C, A>& rhs) {
        auto obj = yyjson_mut_obj(doc);
        for (const auto& kv : rhs) {
            auto key            = yyjson_mut_strncpy(doc, kv.first.c_str(), kv.first.size());
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
            auto key            = yyjson_mut_strncpy(doc, kv.first.c_str(), kv.first.size());
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
struct yyjson_convert<cc::Value> {
    static yyjson_mut_val* to_json(yyjson_mut_doc* doc, const cc::Value& rhs) {
        yyjson_mut_val* val = nullptr;
        auto t              = rhs.type();
        switch (t) {
        case Value::NUL: val = yyjson_mut_null(doc); break;
        case Value::BOOL: val = yyjson_mut_bool(doc, rhs.get<bool>()); break;
        case Value::INTEGER: val = yyjson_mut_int(doc, rhs.get<int>()); break;
        case Value::REAL: val = yyjson_mut_real(doc, rhs.get<double>()); break;
        case Value::STRING:
            val = yyjson_convert<std::string_view>::to_json(doc, rhs.get<std::string_view>());
            break;
        case Value::ARRAY:
            val = yyjson_convert<Value::array_t>::to_json(doc, rhs.get<Value::array_t>());
            break;
        case Value::OBJECT:
            val = yyjson_convert<Value::object_t>::to_json(doc, rhs.get<Value::object_t>());
            break;
        };
        return val;
    }

    static void from_json(yyjson_val* js, cc::Value& rhs) {
        auto t = yyjson_get_type(js);
        switch (t) {
        case YYJSON_TYPE_NONE:
        case YYJSON_TYPE_NULL: rhs = Value(); break;
        case YYJSON_TYPE_BOOL: rhs.set(yyjson_get_bool(js)); break;
        case YYJSON_TYPE_NUM:
            if (yyjson_is_int(js)) {
                rhs.set(yyjson_get_int(js));
            } else {
                rhs.set(yyjson_get_real(js));
            }
            break;
        case YYJSON_TYPE_STR: rhs.set(std::string(yyjson_get_str(js), yyjson_get_len(js))); break;
        case YYJSON_TYPE_ARR: {
            Value::array_t arr(yyjson_arr_size(js));
            yyjson_val* elem;
            size_t idx, max;
            yyjson_arr_foreach(js, idx, max, elem) {
                yyjson_convert<Value>::from_json(elem, arr.at(idx));
            }
            rhs.set(std::move(arr));
            break;
        }
        case YYJSON_TYPE_OBJ: {
            Value::object_t obj(yyjson_obj_size(js));
            yyjson_val* key;
            yyjson_val* val;
            size_t idx, max;
            yyjson_obj_foreach(js, idx, max, key, val) {
                std::string str_key = yyjson_get_str(key);
                Value value;
                yyjson_convert<Value>::from_json(val, value);
                obj.emplace(std::move(str_key), std::move(value));
            }
            rhs.set(std::move(obj));
            break;
        }
        default: ASSERT(false, "Unknown json type !!!");
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
            using Member       = std::remove_const_t<std::remove_reference_t<decltype(member)>>;

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
    JsonParser(std::string_view jstr, bool allow_comments) {
        doc_ =
            yyjson_read(jstr.data(), jstr.size(), allow_comments ? YYJSON_READ_ALLOW_COMMENTS : 0);
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
T parse(std::string_view jstr, bool allow_comments = false) {
    JsonParser parser(jstr, allow_comments);
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
