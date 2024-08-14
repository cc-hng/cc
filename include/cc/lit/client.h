#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/url.hpp>
#include <fmt/core.h>

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
    beast::flat_buffer buffer;

    Connection(tcp_stream&& s, std::string h, std::string p, bool t)
      : stream(std::move(s))
      , host(std::move(h))
      , port(std::move(p))
      , tls(t) {}

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

    void release() {
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
    std::shared_ptr<detail::Connection> conn;
    http::response<Body> res;

    try {
        boost::urls::url_view u(url);
        auto scheme      = u.scheme();
        auto host        = u.host();
        auto target      = u.path();
        std::string port = u.port();
        if (port.empty()) {
            if (scheme == "http") {
                port = "80";
            } else if (scheme == "https") {
                port = "443";
            }
        }
        if (port.empty()) {
            throw std::runtime_error("unknown port");
        }

        std::string requri = target.empty() ? "/" : target;
        if (u.has_query()) {
            requri += "?";
            requri += u.query();
        }

        conn = co_await pool.get_connection(host, port, scheme == "https" ? true : false);

        http::request<http::string_body> req{options.method, requri, 11};
        req.set(http::field::host, host);
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

        conn->buffer.clear();
        std::ignore = co_await http::async_read(conn->stream, conn->buffer, res);

        if (options.keepalive) {
            pool.save_connection(conn);
        }
    } catch (...) {
        if (conn && options.keepalive) {
            pool.save_connection(conn);
        }
        throw;
    }

    co_return res;
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

inline void fetch_pool_release() {
    detail::ConnectionPool::instance().release();
}

}  // namespace lit
}  // namespace cc
