#pragma once

#include <string>

///////////////////////////////////////////////////////////////////////
// common message type
// clang-format off
struct cc_empty_msg_t {};

template <typename T>
struct cc_common_msg_t {
    int code;
    std::string msg;
    T data;
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
struct api_a_req_t {
    int a;
};

struct api_a_rep_t {
    int a;
};

struct api_b_request_t {
    int b;
};

struct api_b_reply_t {
    int b;
};
