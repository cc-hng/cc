#pragma once

#include <string>
#include <boost/hana.hpp>

///////////////////////////////////////////////////////////////////////
// common message type
// clang-format off
struct cc_empty_msg_t { BOOST_HANA_DEFINE_STRUCT(cc_empty_msg_t); };

template <typename T>
struct cc_common_msg_t {
    BOOST_HANA_DEFINE_STRUCT(cc_common_msg_t,
        (int, code),
        (std::string, msg),
        (T, data));
};

template <typename T>
auto make_reply(T&& data) -> cc_common_msg_t<T> {
    cc_common_msg_t<T> ret;
    ret.code = 200;
    ret.msg = "";
    ret.data = std::forward<T>(data);
    return ret;
}

[[maybe_unused]]
static cc_common_msg_t<cc_empty_msg_t>
make_reply(int code, std::string_view msg = "") {
    cc_common_msg_t<cc_empty_msg_t> ret;
    ret.code = code;
    ret.msg = msg;
    return ret;
}
// clang-format on

///////////////////////////////////////////////////////////////////////
// clang-format off

struct api_a_req_t {
    BOOST_HANA_DEFINE_STRUCT(api_a_req_t,
        (int, a));
};

struct api_a_rep_t {
    BOOST_HANA_DEFINE_STRUCT(api_a_rep_t,
        (int, a));
};

// clang-format on

struct api_b_request_t {
    int a;
};
BOOST_HANA_ADAPT_STRUCT(api_b_request_t, a);

struct api_b_reply_t {
    int a;
};
BOOST_HANA_ADAPT_STRUCT(api_b_reply_t, a);
