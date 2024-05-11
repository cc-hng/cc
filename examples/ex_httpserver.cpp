
#include "msg.h"
#include <boost/callable_traits.hpp>
#include <boost/hana.hpp>
#include <cc/asio/core.h>
#include <cc/json.h>
#include <cc/lit/core.h>
#include <cc/lit/util.h>
#include <cc/type_traits.h>
#include <fmt/core.h>

class HttpServer {
public:
    HttpServer(boost::asio::io_context& ctx, std::string_view ip, int port)
      : app_(ctx, ip, port)
      , origin_(10) {}

    void start() {
        app_.Get("/api/a",
                 [](const auto& req, auto& resp,
                    const auto& go) -> boost::asio::awaitable<void> {
                     //  co_await cc::async_sleep(1000);
                     resp.set_content("<p1>hello,world</p1>", "text/html");
                     co_return;
                 });

        app_.Get("/api/b", [](const auto& req, auto& resp) {
            resp.set_content("<p1>hello,world2</p1>", "text/html");
        });

        app_.Get("/api/c", [](const auto& req, auto& resp) -> boost::asio::awaitable<void> {
            resp.set_content("<p1>hello,world3</p1>", "text/html");
            co_return;
        });

        app_.Post("/api/e", cc::make_route(&HttpServer::api_e, this));
        app_.Post("/api/f", cc::make_route(&HttpServer::api_f, this));
        app_.start();
    }

private:
    api_a_rep_t api_e(const api_a_req_t& req) { return api_a_rep_t{req.a * 5}; }

    asio::awaitable<api_a_rep_t> api_f(const api_a_req_t& req) {
        co_await cc::async_sleep(1000);
        co_return api_a_rep_t{req.a * 5 + origin_};
    }

private:
    cc::lit::App app_;
    int origin_;
};

int main(int argc, char* argv[]) {
    auto& Ap  = cc::AsioPool::get();
    auto& ctx = Ap.get_io_context();

    HttpServer server(ctx, "0.0.0.0", 8088);
    fmt::print("Server listen at: *:8088\n");

    server.start();

    Ap.run(1);
    return 0;
}
