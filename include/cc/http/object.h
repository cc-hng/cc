#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace cc {

namespace http = boost::beast::http;

struct http_request_t {
    using kv_t     = std::shared_ptr<std::unordered_map<std::string, std::string>>;
    using raw_type = http::request<http::span_body<char>>;

    raw_type request;
    kv_t params;
    kv_t querys;

    raw_type* operator->() { return &request; }
    const raw_type* operator->() const { return &request; }
};

struct http_response_t {
    using raw_type = http::response<http::string_body>;
    raw_type response;

    http_response_t() {
        response.result(http::status::not_found);
        response.body() = "Not Found\n";
    }

    raw_type* operator->() { return &response; }
    const raw_type* operator->() const { return &response; }

    void set_content(std::string_view body, std::string_view content_type = "text/"
                                                                            "plain") {
        response.result(http::status::ok);
        response.body() = body;
        response.set(http::field::content_type, content_type);
    }
};

using http_next_handle_t = std::function<boost::asio::awaitable<void>()>;
using http_handle_t =
    std::function<boost::asio::awaitable<void>(const http_request_t&, http_response_t&,
                                               const http_next_handle_t&)>;

}  // namespace cc
