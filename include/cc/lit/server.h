#pragma once

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <list>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/callable_traits.hpp>
#include <boost/core/noncopyable.hpp>
#include <cc/asio/semaphore.h>
#include <cc/lit/middleware.h>
#include <cc/lit/mime_types.h>
#include <cc/lit/object.h>
#include <cc/lit/router.h>
#include <cc/type_traits.h>
#include <gsl/gsl>

namespace cc {
namespace lit {

namespace detail {
// Return a reasonable mime type based on the extension of a file.
inline std::string_view get_mime_type(std::string_view path) {
    static auto get_ext = [](std::string_view path) {
        auto const pos = path.rfind(".");
        if (GSL_UNLIKELY(pos == std::string_view::npos)) return std::string_view{};
        return path.substr(pos);
    };
    return cc::lit::get_mime_type(get_ext(path));
}
}  // namespace detail

class App final : boost::noncopyable {
public:
    struct options_t {
        int timeout    = 30;
        int backlog    = 128;
        int body_limit = -1;
    };

    struct static_router_t {
        std::string mount_point;
        std::string doc_root;
        std::regex re;

        static_router_t(std::string_view _mount_point, std::string_view _doc_root)
          : mount_point(_mount_point)
          , doc_root(_doc_root)
          , re("^" + std::string(_mount_point) + "([\\w\\./-]+)[?#]?.*$") {}

        std::tuple<bool, std::optional<http::message_generator>>
        operator()(const http::request<http::string_body>& req) const {
            namespace fs = std::filesystem;
            auto method  = req.method();
            if (GSL_UNLIKELY(method != http::verb::head && method != http::verb::get)) {
                return std::make_tuple(false, std::nullopt);
            }

            auto target = req.target();
            auto p      = static_cast<const char*>(target.data());
            std::cmatch cm;
            if (!std::regex_match(p, p + target.size(), cm, re)) {
                return std::make_tuple(false, std::nullopt);
            }

            auto path  = path_cat(doc_root, cm[1].str());
            auto path0 = path;
            if (path.back() == '/') {
                path.append("index.html");
            }

            std::optional<http::message_generator> msg;
            beast::error_code ec;
            http::file_body::value_type body;
            if (!fs::exists(path) && path0.back() == '/' && fs::is_directory(path0)) {
                std::ostringstream oss;
                oss << R"(<!DOCTYPE html> <html> <head> </head> <body>)";
                oss << "<h3>Directory: " << path0 << "</h3> <ul>";
                try {
                    // 收集并排序目录条目
                    std::vector<fs::directory_entry> entries;
                    for (const auto& entry : fs::directory_iterator(path0)) {
                        entries.push_back(entry);
                    }

                    // 先显示目录，后显示文件
                    std::sort(entries.begin(), entries.end(),
                              [](const fs::directory_entry& a, const fs::directory_entry& b) {
                                  if (fs::is_directory(a) != fs::is_directory(b)) {
                                      return fs::is_directory(a);
                                  }
                                  return a.path().filename() < b.path().filename();
                              });

                    // 生成列表项
                    for (const auto& entry : entries) {
                        std::string name = entry.path().filename().string();
                        std::string back = fs::is_directory(entry) ? "/" : "";

                        oss << "            <li><a href=\"" << mount_point << cm[1].str() << name
                            << back << "\">" << name << back << "</a></li>\n";
                    }

                } catch (const std::exception& e) {
                    oss << "            <li>Error: " << e.what() << "</li>\n";
                }

                // HTML footer
                oss << R"(        </ul> </body> </html>)";

                http::response<http::string_body> resp{http::status::ok, req.version()};
                resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                resp.set(http::field::content_type, "text/html");
                resp.body() = oss.str();
                resp.prepare_payload();
                msg = std::move(resp);
                return std::make_tuple(true, std::move(msg));
            }

            if (!fs::is_directory(path)) {
                body.open(path.c_str(), beast::file_mode::scan, ec);
                if (ec == beast::errc::no_such_file_or_directory) {
                    return std::make_tuple(false, std::nullopt);
                }
            } else {
                return std::make_tuple(false, std::nullopt);
            }

            if (GSL_UNLIKELY(ec)) {
                http::response<http::string_body> resp{http::status::internal_server_error,
                                                       req.version()};
                resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                resp.set(http::field::content_type, detail::get_mime_type(path));
                resp.body() = ec.what();
                resp.keep_alive(req.keep_alive());
                msg = std::move(resp);
                return std::make_tuple(true, std::move(msg));
            }

            // Cache the size since we need it after the move
            auto const size = body.size();

            // Respond to HEAD request
            if (GSL_UNLIKELY(method == http::verb::head)) {
                http::response<http::empty_body> resp{http::status::ok, req.version()};
                resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                resp.set(http::field::content_type, detail::get_mime_type(path));
                resp.content_length(size);
                resp.keep_alive(req.keep_alive());
                msg = std::move(resp);
            } else {
                http::response<http::file_body> resp{std::piecewise_construct,
                                                     std::make_tuple(std::move(body)),
                                                     std::make_tuple(http::status::ok,
                                                                     req.version())};
                resp.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                resp.set(http::field::content_type, detail::get_mime_type(path));
                resp.content_length(size);
                resp.keep_alive(req.keep_alive());
                msg = std::move(resp);
            }

            return std::make_tuple(true, std::move(msg));
        }

        // Append an HTTP rel-path to a local filesystem path.
        // The returned path is normalized for the platform.
        static inline std::string path_cat(beast::string_view base, beast::string_view path) {
            if (base.empty()) return std::string(path);
            std::string result(base);
#ifdef BOOST_MSVC
            char constexpr path_separator = '\\';
            if (result.back() == path_separator) result.resize(result.size() - 1);
            result.append(path.data(), path.size());
            for (auto& c : result)
                if (c == '/') c = path_separator;
#else
            char constexpr path_separator = '/';
            if (result.back() == path_separator) result.resize(result.size() - 1);
            result.append(path.data(), path.size());
#endif
            return result;
        }
    };

public:
    App(boost::asio::io_context& ioc, std::string_view ip, unsigned short port,
        options_t options = {30, 128, -1})
      : ioc_(ioc)
      , ip_(ip)
      , port_(port)
      , option_(options) {
        router_.use(&middleware::auto_headers);
    }

    ~App() = default;

    /// run
    void start() {
        auto address = asio::ip::make_address(ip_);
        asio::co_spawn(ioc_, do_listen(tcp::endpoint{address, port_}), handle_exception);
    }

    /// static
    void serve_static(std::string_view mount_point, std::string_view dir) {
        static_router_.emplace_back(static_router_t(mount_point, dir));
    }

    void websocket(std::string_view path, ws_handle_t&& handler) {
        class Functor {
        public:
            Functor(std::string_view path, ws_handle_t&& handler)
              : parse_path_(Router::compile_route(path))
              , handler_(std::move(handler)) {}

            asio::awaitable<bool>
            operator()(const http_request_t& req, std::shared_ptr<tcp_stream> stream) {
                auto [matched, params] = parse_path_(req.path);
                if (!matched) {
                    co_return false;
                }

                req.params = params;

                ws_stream ws(stream->release_socket());
                stream = nullptr;
                ws.set_option(
                    beast::websocket::stream_base::timeout::suggested(beast::role_type::server));

                // Accept the websocket handshake
                co_await ws.async_accept(req.request);
                co_await handler_(req, std::move(ws));
                co_return true;
            }

        private:
            std::function<std::tuple<bool, http_request_t::kv_t>(std::string_view)> parse_path_;
            ws_handle_t handler_;
        };

        ws_router_.emplace_back(Functor(path, std::move(handler)));
    }

    /// route
    template <typename Fn = http_handle_t>
    App& use(Fn&& handler) {
        // router_.use(std::move(handler));
        router_.use(make_handle(std::forward<Fn>(handler)));
        return *this;
    }

    template <typename Fn>
    App& route(http::verb method, std::string_view path, Fn&& handler) {
        router_.route(method, path, make_handle(std::forward<Fn>(handler)));
        return *this;
    }

    void Get(std::string_view path, auto&& handler) {
        route(http::verb::get, path, std::move(handler));
    }

    void Post(std::string_view path, auto&& handler) {
        route(http::verb::post, path, std::move(handler));
    }

    void Any(std::string_view path, auto&& handler) {
        route((http::verb)-1, path, std::move(handler));
    }

private:
    template <typename Fn>
    static http_handle_t make_handle(Fn&& f) {
        http_handle_t f0;
        if constexpr (cc::is_callable_v<Fn, const http_request_t&, http_response_t&,
                                        const http_next_handle_t&>) {
            f0 = std::forward<Fn>(f);
        } else {
            using Ret = cc::result_of_t<Fn(const http_request_t&, http_response_t&)>;
            f0        = [f = std::forward<Fn>(
                      f)](const http_request_t& req, http_response_t& resp,
                          const http_next_handle_t& go) -> boost::asio::awaitable<void> {
                if constexpr (cc::is_awaitable_v<Ret>) {
                    co_await f(req, resp);
                } else {
                    co_return f(req, resp);
                }
            };
        }
        return f0;
    }

    static void handle_exception(std::exception_ptr e) {
        if (!e) return;
        try {
            std::rethrow_exception(e);
        } catch (boost::system::system_error& e) {
            bool ignore = e.code() == asio::error::connection_reset
                          || e.code() == asio::error::timed_out
                          || e.code() == boost::beast::error::timeout;
            if (!ignore) {
                std::cerr << "Error in session: " << e.what() << "\n";
            }
        } catch (std::exception& e) {
            std::cerr << "Error in session: " << e.what() << "\n";
        }
    }

    // Accepts incoming connections and launches the sessions
    asio::awaitable<void> do_listen(tcp::endpoint endpoint) {
        // Open the acceptor
        auto acceptor =
            asio::use_awaitable.as_default_on(tcp::acceptor(co_await asio::this_coro::executor));
        acceptor.open(endpoint.protocol());

        // Disallow address reuse
        acceptor.set_option(asio::socket_base::reuse_address(true));

        // Bind to the server address
        acceptor.bind(endpoint);

        // Start listening for connections
        // const auto backlog = asio::socket_base::max_listen_connections;
        const auto backlog = option_.backlog;
        acceptor.listen(asio::socket_base::max_listen_connections);
        Semaphore<std::mutex> sem(backlog);

        for (;;) {
            co_await sem.acquire();
            auto stream = std::make_shared<tcp_stream>(co_await acceptor.async_accept());
            asio::co_spawn(acceptor.get_executor(), do_session(stream), [&](auto e) {
                sem.release();
                handle_exception(e);
            });
        }
    }

    // Handles an HTTP server connection
    asio::awaitable<void> do_session(std::shared_ptr<tcp_stream> stream) {
        // This buffer is required to persist across reads
        beast::flat_buffer buffer;

        try {
            for (;;) {
                std::optional<http::message_generator> msg;
                // Set the timeout.
                stream->expires_after(
                    std::chrono::seconds(option_.timeout > 0 ? option_.timeout : INT_MAX));

                // Read a request
                http_request_t req;
                http_response_t resp;

                http::request_parser<http::string_body> req_parser;
                if (option_.body_limit > 0) {
                    req_parser.body_limit(option_.body_limit);
                }
                co_await http::async_read(*stream, buffer, req_parser);
                req.request = req_parser.release();

                if (GSL_UNLIKELY(!req.handle())) {
                    resp->keep_alive(req->keep_alive());
                    resp->result(http::status::bad_request);
                    resp->body() = "Illegal request-target\n";
                } else {
                    if (GSL_UNLIKELY(beast::websocket::is_upgrade(req.request))) {
                        resp->keep_alive(req->keep_alive());
                        for (const auto& f : ws_router_) {
                            if (co_await f(req, stream)) {
                                break;
                            }
                        }
                    } else {
                        co_await router_.run(req, resp, nullptr);

                        /// API not found
                        /// Fallback to static file
                        if (resp->result() == http::status::not_found) {
                            for (const auto& r : static_router_) {
                                auto [matched, response] = r(req.request);
                                if (matched) {
                                    msg = std::move(response);
                                    break;
                                }
                            }
                        }
                    }
                }

                if (!msg.has_value()) {
                    resp->prepare_payload();
                    *msg = (std::move(resp.response));
                }

                // Determine if we should close the connection
                bool keep_alive = msg->keep_alive();

                // Send the response
                co_await beast::async_write(*stream, std::move(*msg), asio::use_awaitable);

                if (!keep_alive) {
                    // This means we should close the connection, usually because
                    // the response indicated the "Connection: close" semantic.
                    break;
                }
            }
        } catch (boost::system::system_error& se) {
            auto code = se.code();
            if (code != http::error::end_of_stream && code != asio::error::connection_reset
                && code != asio::error::timed_out && code != asio::error::operation_aborted
                && code != beast::websocket::error::closed) {
                throw;
            }
        }

        // Send a TCP shutdown
        if (stream) {
            beast::error_code ec;
            std::ignore = stream->socket().shutdown(tcp::socket::shutdown_send, ec);
        }

        // At this point the connection is closed gracefully
        // we ignore the error because the client might have
        // dropped the connection already.
    }

private:
    boost::asio::io_context& ioc_;
    const std::string ip_;
    const unsigned short port_;
    const options_t option_;
    Router router_;
    std::list<static_router_t> static_router_;
    std::list<std::function<asio::awaitable<bool>(const http_request_t&, std::shared_ptr<tcp_stream>)>>
        ws_router_;
};

}  // namespace lit
}  // namespace cc
