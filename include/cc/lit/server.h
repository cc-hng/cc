#pragma once

#include <algorithm>
#include <memory>
#include <regex>
#include <string>
#include <vector>
#include <boost/noncopyable.hpp>
#include <cc/lit/middleware.h>
#include <cc/lit/object.h>
#include <cc/lit/router.h>
#include <cc/type_traits.h>
#include <stdint.h>

namespace cc {
namespace lit {

class App final : boost::noncopyable {
public:
    using ReqBody       = http_request_body_t;
    using RespBody      = http_response_body_t;
    using router_t      = Router<ReqBody, RespBody>;
    using request_type  = typename router_t::request_type;
    using response_type = typename router_t::response_type;
    using ws_handler0 =
        std::function<net::awaitable<bool>(const request_type&, std::shared_ptr<tcp_stream>)>;
    using on_error_handler = std::function<bool(std::string_view)>;
    struct option_t {
        int body_limit;
        int timeout;
        int backlog;
    };

private:
    net::io_context& ioc_;
    const std::string ip_;
    const uint16_t port_;
    const option_t option_;
    router_t router_;
    std::vector<ws_handler0> ws_router_;
    on_error_handler on_error_;

public:
    App(net::io_context& ctx, std::string_view ip, uint16_t port,
        const option_t& option = {-1, 30, 128})
      : ioc_(ctx)
      , ip_(ip)
      , port_(port)
      , option_(option)
      , on_error_(nullptr) {
        ws_router_.reserve(8);
        router_.use(middleware::auto_headers<ReqBody, RespBody>);
    }

    ~App() = default;

    App& on_error(const on_error_handler& h) {
        on_error_ = h;
        return *this;
    }

    void start() {
        auto address = net::ip::make_address(ip_);
        net::co_spawn(ioc_, do_listen(tcp::endpoint{address, port_}), [this](auto e) {
            try {
                handle_exception(e);
            } catch (std::exception& e0) {
                if (!(on_error_ && on_error_(e0.what()))) {
                    throw;
                }
            }
        });
    }

    App& serve_static(std::string_view mountpoint, std::string_view dir) {
        return use(StaticFileProvider(mountpoint, dir));
    }

    App& websocket(std::string_view path, ws_handler<ReqBody>&& handler) {
        class Functor {
        public:
            Functor(std::string_view path, ws_handler<ReqBody>&& handler)
              : parse_path_(router_t::compile_route(path))
              , handler_(std::move(handler)) {}

            net::awaitable<bool>
            operator()(const http_request_t<ReqBody>& req, std::shared_ptr<tcp_stream> stream) {
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
                co_await ws.async_accept(req.raw);
                co_await handler_(req, std::move(ws));
                co_return true;
            }

        private:
            std::function<std::tuple<bool, router_t::kv_t>(std::string_view)> parse_path_;
            ws_handler<ReqBody> handler_;
        };

        ws_router_.emplace_back(Functor(path, std::move(handler)));
        return *this;
    }

    template <typename H = router_t::route_handler>
    App& use(H&& h) {
        router_.use(make_handle(std::forward<H>(h)));
        return *this;
    }

    template <typename H>
    App& route(http::verb method, std::string_view path, H&& h) {
        router_.route(method, path, make_handle(std::forward<H>(h)));
        return *this;
    }

    App& Get(std::string_view path, auto&& h) {
        return route(http::verb::get, path, std::move(h));
    }

    App& Post(std::string_view path, auto&& h) {
        return route(http::verb::post, path, std::move(h));
    }

    App& Any(std::string_view path, auto&& h) { return route((http::verb)-1, path, std::move(h)); }

private:
    template <typename Handler>
    static auto make_handle(Handler&& h) -> typename router_t::route_handler {
        using F = router_t::route_handler;
        F f;
        if constexpr (is_callable_v<Handler, const router_t::request_type&,
                                    router_t::response_type&, const router_t::next_handler&>) {
            f = std::forward<Handler>(h);
        } else {
            static_assert(
                is_callable_v<Handler, const router_t::request_type&, router_t::response_type&>,
                "Unsupport handler!!!");
            using R = result_of_t<Handler(const router_t::request_type&, router_t::response_type&)>;
            f = [h = std::forward<Handler>(h)]  //
                (const router_t::request_type& req, router_t::response_type& resp,
                 const router_t::next_handler&) -> net::awaitable<void> {
                if constexpr (is_awaitable_v<R>) {
                    co_await h(req, resp);
                } else {
                    co_return h(req, resp);
                }
            };
        }
        return f;
    }

    static void handle_exception(std::exception_ptr e) {
        if (!e) return;

        try {
            std::rethrow_exception(e);
        } catch (boost::system::system_error& e) {
            bool ignore = e.code() == net::error::connection_reset
                          || e.code() == net::error::timed_out
                          || e.code() == boost::beast::error::timeout;
            if (!ignore) {
                throw;
            }
        } catch (std::exception& e) {
            throw;
        }
    }

    // Accepts incoming connections and launches the sessions
    net::awaitable<void> do_listen(tcp::endpoint endpoint) {
        // Open the acceptor
        auto acceptor =
            net::use_awaitable.as_default_on(tcp::acceptor(co_await net::this_coro::executor));
        acceptor.open(endpoint.protocol());

        // Disallow address reuse
        acceptor.set_option(net::socket_base::reuse_address(true));

        // Bind to the server address
        acceptor.bind(endpoint);

        // Start listening for connections
        // const auto backlog = net::socket_base::max_listen_connections;
        const auto backlog = option_.backlog;
        acceptor.listen(net::socket_base::max_listen_connections);

        for (;;) {
            auto stream = std::make_shared<tcp_stream>(co_await acceptor.async_accept());
            net::co_spawn(acceptor.get_executor(), do_session(stream), [this](auto e) {
                try {
                    handle_exception(e);
                } catch (std::exception& e0) {
                    if (!(on_error_ && on_error_(e0.what()))) {
                        throw;
                    }
                }
            });
        }
    }

    // Handles an HTTP server connection
    net::awaitable<void> do_session(std::shared_ptr<tcp_stream> stream) {
        // This buffer is required to persist across reads
        beast::flat_buffer buffer;

        try {
            for (;;) {
                // Set the timeout.
                stream->expires_after(
                    std::chrono::seconds(option_.timeout > 0 ? option_.timeout : INT_MAX));

                router_t::request_type req;
                router_t::response_type resp;

                http::request_parser<ReqBody> request_parser;
                if (option_.body_limit > 0) {
                    request_parser.body_limit(option_.body_limit);
                }
                co_await http::async_read(*stream, buffer, request_parser);
                req.raw = request_parser.release();

                if (GSL_LIKELY(req.compile_target())) {
                    if (GSL_UNLIKELY(beast::websocket::is_upgrade(req.raw))) {
                        resp->keep_alive(req->keep_alive());
                        for (const auto& f : ws_router_) {
                            if (co_await f(req, stream)) {
                                break;
                            }
                        }
                    } else {
                        co_await router_.process(req, resp, nullptr);
                    }
                } else {
                    resp->keep_alive(req->keep_alive());
                    resp->result(http::status::bad_request);
                    resp->body() = "Illegal request-target\n";
                    resp->prepare_payload();
                }

                http::message_generator msg(std::move(resp.raw));
                // Determine if we should close the connection
                bool keep_alive = msg.keep_alive();

                // Send the response
                co_await beast::async_write(*stream, std::move(msg), net::use_awaitable);

                if (!keep_alive) {
                    // This means we should close the connection, usually because
                    // the response indicated the "Connection: close" semantic.
                    break;
                }
            }
        } catch (boost::system::system_error& se) {
            auto code = se.code();
            if (code != http::error::end_of_stream && code != net::error::connection_reset
                && code != net::error::broken_pipe && code != net::error::timed_out
                && code != net::error::operation_aborted && code != beast::websocket::error::closed) {
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
};

}  // namespace lit
}  // namespace cc
