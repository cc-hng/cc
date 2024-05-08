#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace cc {
namespace lit {

namespace asio   = boost::asio;
namespace beast  = boost::beast;
namespace http   = beast::http;
using tcp        = boost::asio::ip::tcp;
using tcp_stream = typename beast::tcp_stream::rebind_executor<
    asio::use_awaitable_t<>::executor_with_default<asio::any_io_executor>>::other;
using ws_stream = beast::websocket::stream<tcp_stream>;

struct http_request_t {
    using kv_t     = std::shared_ptr<std::unordered_map<std::string, std::string>>;
    using raw_type = http::request<http::span_body<char>>;

    raw_type request;
    std::string_view path;
    kv_t querys;
    kv_t params;

    raw_type* operator->() { return &request; }
    const raw_type* operator->() const { return &request; }

    bool handle() {
        auto [path, query] = split_target(request.target());
        this->path         = path;
        if (!query.empty()) {
            typename kv_t::element_type out;
            const char* s = &*query.begin();
            int left      = query.size();
            int len;

            while (left > 0) {
                for (len = 0; len < left; len++) {
                    if (s[len] == '&') {
                        break;
                    }
                }

                int j = 0;
                for (j = 0; j < len; j++) {
                    if (s[j] == '=') {
                        break;
                    }
                }

                if (j + 1 >= len) {
                    return false;
                }
                std::string key(s, j);
                std::string val(s + j + 1, len - j - 1);
                out.emplace((std::string&&)key, (std::string&&)val);

                s += len + 1;
                left -= len + 1;
            }

            querys = std::make_shared<typename kv_t::element_type>(std::move(out));
        }
        return true;
    }

    static std::tuple<std::string_view, std::string_view>
    split_target(std::string_view target) {
        static std::string nullstr = "";
        auto offset                = target.find_first_of('?');
        std::string_view path, query;
        if (offset != std::string_view::npos) {
            path  = target.substr(0, offset);
            query = target.substr(offset + 1);
        } else {
            path  = target;
            query = nullstr;
        }
        return std::make_tuple(path, query);
    }
};

struct http_response_t {
    using raw_type = http::response<http::string_body>;
    raw_type response;

    http_response_t() {
        response.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        response.result(http::status::not_found);
        response.body() = "Not Found\n";
    }

    raw_type* operator->() { return &response; }
    const raw_type* operator->() const { return &response; }

    void set_content(std::string_view body,
                     std::string_view content_type = "application/json") {
        response.result(http::status::ok);
        response.body() = body;
        response.set(http::field::content_type, content_type);
    }
};

using http_next_handle_t = std::function<boost::asio::awaitable<void>()>;
using http_handle_t =
    std::function<boost::asio::awaitable<void>(const http_request_t&, http_response_t&,
                                               const http_next_handle_t&)>;
using ws_handle_t = std::function<asio::awaitable<void>(const http_request_t&, ws_stream)>;

}  // namespace lit
}  // namespace cc
