#pragma once

#include <functional>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace cc {
namespace lit {

namespace detail {
//
/// @urlencode / urldecode
[[maybe_unused]] static std::string urlencode(std::string_view raw) {
    std::ostringstream oss;
    oss.fill('0');
    oss << std::hex;
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else {
            oss << '%' << std::setw(2) << std::uppercase << (int)c;
        }
    }
    return oss.str();
}

[[maybe_unused]] static std::string urldecode(std::string_view raw) {
    std::string result;
    result.reserve(raw.size());

    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '%' && i + 2 < raw.size()) {
            int value;
            std::istringstream is(std::string(raw.substr(i + 1, 2)));
            if (is >> std::hex >> value) {
                result += static_cast<char>(value);
                i += 2;
            }
        } else if (raw[i] == '+') {
            result += ' ';
        } else {
            result += raw[i];
        }
    }

    return result;
}

}  // namespace detail

namespace asio   = boost::asio;
namespace beast  = boost::beast;
namespace http   = beast::http;
using tcp        = boost::asio::ip::tcp;
using tcp_stream = typename beast::tcp_stream::rebind_executor<
    asio::use_awaitable_t<>::executor_with_default<asio::any_io_executor>>::other;
using ws_stream = beast::websocket::stream<tcp_stream>;

struct http_request_t {
    using kv_t     = std::shared_ptr<std::unordered_map<std::string, std::string>>;
    using raw_type = http::request<http::string_body>;

    raw_type request;
    std::string_view path;
    mutable kv_t querys;
    mutable kv_t params;

    raw_type* operator->() { return &request; }
    const raw_type* operator->() const { return &request; }

    bool handle() {
        auto [path, query] = split_target(request.target());
        this->path         = path;
        if (!query.empty()) {
            typename kv_t::element_type out;
            auto query0   = detail::urldecode(query);
            const char* s = query0.data();
            int left      = query0.size();
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

    static std::tuple<std::string_view, std::string_view> split_target(std::string_view target) {
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
        response.body() = "Not Found";
    }

    raw_type* operator->() { return &response; }
    const raw_type* operator->() const { return &response; }

    void set_content(std::string_view body, std::string_view content_type = "application/json") {
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
