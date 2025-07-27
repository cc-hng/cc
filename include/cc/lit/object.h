#pragma once

#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace cc {
namespace lit {

namespace net    = boost::asio;
namespace beast  = boost::beast;
namespace http   = beast::http;
using tcp        = net::ip::tcp;
using tcp_stream = typename beast::tcp_stream::rebind_executor<
    net::use_awaitable_t<>::executor_with_default<net::any_io_executor>>::other;
using ws_stream = beast::websocket::stream<tcp_stream>;

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

template <typename Body>
struct http_request_t {
    using kv_t     = std::unordered_map<std::string, std::string>;
    using raw_type = http::request<Body>;

    raw_type raw;
    std::string_view path;
    std::optional<kv_t> queries;         // ?a=b&c=d
    mutable std::optional<kv_t> params;  // compile route path(/:user/:name)

    inline raw_type* operator->() { return &raw; }
    inline const raw_type* operator->() const { return &raw; }

    bool compile_target() {
        auto [path, query] = split_target(raw.target());
        this->path         = path;
        if (!query.empty()) {
            kv_t out;
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

            queries = out;
        }
        return true;
    }

    static std::tuple<std::string_view, std::string_view>  //
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

template <typename Body>
struct http_response_t {
    using raw_type = http::response<Body>;
    raw_type raw;

    inline raw_type* operator->() noexcept { return &raw; }
    inline const raw_type* operator->() const noexcept { return &raw; }

    std::enable_if_t<std::is_same_v<Body, http::string_body>, void>
    set_content(std::string_view body, std::string_view content_type = "application/json") {
        raw.result(http::status::ok);
        raw.body() = body;
        raw.set(http::field::content_type, content_type);
    }
};

using http_request_body_t  = http::string_body;
using http_response_body_t = http::string_body;

using http_next_handler = std::function<net::awaitable<void>()>;
template <typename ReqBody, typename RespBody>
using http_route_handler =
    std::function<net::awaitable<void>(const http_request_t<ReqBody>&, http_response_t<RespBody>&,
                                       const http_next_handler&)>;
template <typename ReqBody>
using ws_handler = std::function<net::awaitable<void>(const http_request_t<ReqBody>&, ws_stream)>;

}  // namespace lit
}  // namespace cc
