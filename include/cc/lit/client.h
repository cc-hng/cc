#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/url.hpp>
#include <cc/lit/multipart_parser.h>
#include <fmt/core.h>
#include <gsl/gsl>

namespace cc {
namespace lit {

namespace beast = boost::beast;
namespace http  = beast::http;
namespace asio  = boost::asio;
using tcp       = boost::asio::ip::tcp;
using namespace std::chrono_literals;

namespace detail {

using tcp_stream = typename beast::tcp_stream::rebind_executor<
    asio::use_awaitable_t<>::executor_with_default<asio::any_io_executor>>::other;

struct Connection {
    tcp_stream stream;
    std::string host;
    std::string port;
    bool tls;
    bool first;
    beast::flat_buffer buffer;

    Connection(tcp_stream&& s, std::string h, std::string p, bool t, bool f = true)
      : stream(std::move(s))
      , host(std::move(h))
      , port(std::move(p))
      , tls(t)
      , first(f) {}

    ~Connection() {
        // Gracefully close the socket
        beast::error_code ec;
        std::ignore = stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    }
};

class ConnectionPool : boost::noncopyable {
public:
    static ConnectionPool& instance() {
        static ConnectionPool ins;
        return ins;
    }

    ~ConnectionPool() = default;

    void cleanup() {
        std::unique_lock<std::mutex> lock(mtx_);
        connections_ = {};
    }

    asio::awaitable<std::shared_ptr<Connection>>
    get_connection(const std::string& host, const std::string& port, bool tls) {
        std::unique_lock<std::mutex> lock(mtx_);
        std::string key = host + ":" + port;
        auto& queue     = connections_[key];

        if (!queue.empty()) {
            auto conn = queue.front();
            queue.pop_front();
            co_return conn;
        }

        lock.unlock();
        auto resolver =
            asio::use_awaitable.as_default_on(tcp::resolver(co_await asio::this_coro::executor));
        tcp_stream stream = asio::use_awaitable.as_default_on(
            beast::tcp_stream(co_await asio::this_coro::executor));

        auto const results = co_await resolver.async_resolve(host, port);
        co_await stream.async_connect(results);

        if (tls) {
            // 处理 TLS 连接初始化（如有需要）
        }

        co_return std::make_shared<Connection>(std::move(stream), host, port, tls);
    }

    void save_connection(std::shared_ptr<Connection> conn) {
        std::unique_lock<std::mutex> lock(mtx_);
        conn->first     = false;
        std::string key = conn->host + ":" + conn->port;
        auto& queue     = connections_[key];
        queue.push_back(conn);
    }

private:
    std::mutex mtx_;
    std::unordered_map<std::string, std::deque<std::shared_ptr<Connection>>> connections_;
};
}  // namespace detail

struct fetch_option_t {
    using method_type  = boost::beast::http::verb;
    using headers_type = std::unordered_map<std::string, std::string>;

    method_type method   = method_type::get;
    headers_type headers = {};
    std::string body     = "";
    int timeout          = 30;  // seconds
    bool keepalive       = false;
};

template <typename Body = beast::http::string_body>
asio::awaitable<beast::http::response<Body>>
fetch(std::string_view url, const fetch_option_t options = {}) {
    detail::ConnectionPool& pool = detail::ConnectionPool::instance();
    boost::urls::url_view u(url);
    auto scheme      = u.scheme();
    auto host        = u.host();
    auto host0       = host;
    auto target      = u.path();
    std::string port = u.port();
    if (port.empty()) {
        if (scheme == "http") {
            port = "80";
        } else if (scheme == "https") {
            port = "443";
        }
    } else {
        host0 += ":" + port;
    }
    if (GSL_UNLIKELY(port.empty())) {
        throw std::runtime_error("unknown port");
    }

    std::string requri = target.empty() ? "/" : target;
    if (u.has_query()) {
        requri += "?";
        requri += u.query();
    }

    for (;;) {
        std::shared_ptr<detail::Connection> conn;
        try {
            conn = co_await pool.get_connection(host, port, scheme == "https" ? true : false);

            http::request<http::string_body> req{options.method, requri, 11};
            req.set(http::field::host, host0);
            req.set(http::field::content_type, "application/json");
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            req.keep_alive(options.keepalive);
            req.body() = std::move(options.body);
            for (const auto& kv : options.headers) {
                req.set(kv.first, kv.second);
            }
            req.prepare_payload();

            conn->stream.expires_after(std::chrono::seconds(options.timeout));
            std::ignore = co_await http::async_write(conn->stream, req);

            http::response<Body> res;
            conn->buffer.clear();
            std::ignore = co_await http::async_read(conn->stream, conn->buffer, res);

            if (options.keepalive) {
                pool.save_connection(conn);
            }
            co_return res;
        } catch (...) {
            // 如果连接是keepalive的且抛异常(区分异常)
            // 则尝试新建连接或者从pool取新的连接，重新尝试
            if (conn && !conn->first && options.keepalive) {
                continue;
            } else {
                throw;
            }
        }
    }
}

template <typename Body = beast::http::string_body>
asio::awaitable<beast::http::response<Body>>
http_get(std::string_view url, const fetch_option_t::headers_type& headers, bool keepalive = false) {
    fetch_option_t opt = {
        .headers   = headers,
        .keepalive = keepalive,
    };
    co_return co_await fetch(url, opt);
}

template <typename Body = beast::http::string_body>
asio::awaitable<beast::http::response<Body>> http_get(std::string_view url, bool keepalive = false) {
    co_return co_await http_get(url, {}, keepalive);
}

template <typename Body = beast::http::string_body>
asio::awaitable<beast::http::response<Body>>
http_post(std::string_view url, std::string body, const fetch_option_t::headers_type& headers,
          bool keepalive = false) {
    fetch_option_t opt = {
        .method    = fetch_option_t::method_type::post,
        .headers   = headers,
        .body      = std::move(body),
        .keepalive = keepalive,
    };
    co_return co_await fetch(url, opt);
}

template <typename Body = beast::http::string_body>
asio::awaitable<beast::http::response<Body>>
http_post(std::string_view url, std::string body, bool keepalive = false) {
    co_return co_await http_post(url, std::move(body), {}, keepalive);
}

template <typename Body = http::string_body>
asio::awaitable<beast::http::response<Body>>
http_upload(std::string_view url, const std::vector<multipart_formdata_t>& formdata,
            const fetch_option_t::headers_type& headers, bool keepalive = false) {
    static auto generate_boundary = []() {
        const std::string chars = "0123456789"
                                  "abcdefghijklmnopqrstuvwxyz"
                                  "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::random_device rd;
        std::mt19937 generator(rd());
        std::uniform_int_distribution<> dist(0, chars.size() - 1);

        std::string boundary = "----WebKitFormBoundary";
        for (int i = 0; i < 16; ++i) {  // Generate a 16-character random string
            boundary += chars[dist(generator)];
        }
        return boundary;
    };

    auto boundary = generate_boundary();
    auto body     = multipart_formdata_t::encode(formdata, boundary);
    auto headers0 = headers;
    headers0.emplace("Content-Type", "multipart/form-data; boundary=" + boundary);
    co_return co_await http_post(url, std::move(body), headers0, keepalive);
}

template <typename Body = http::string_body>
asio::awaitable<beast::http::response<Body>>
http_upload(std::string_view url, const std::vector<multipart_formdata_t>& formdata,
            bool keepalive = false) {
    co_return co_await http_upload(url, formdata, {}, keepalive);
}

inline void fetch_pool_cleanup() {
    detail::ConnectionPool::instance().cleanup();
}

}  // namespace lit
}  // namespace cc
