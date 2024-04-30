
#include <cc/asio/core.h>
#include <cc/http/core.h>
#include <fmt/core.h>

int main(int argc, char* argv[]) {
    auto& Ap  = cc::AsioPool::get();
    auto& ctx = Ap.get_io_context();

    cc::HttpServer server(ctx, "0.0.0.0", 8088);

    server.serve_static("/public", "/data/www/html");
    // server.Get("/index.html",
    //            [](const auto& req, auto& resp,
    //               const auto& go) -> boost::asio::awaitable<void> {
    //                resp.set_content("<p1>hello,world</p1>", "text/html");
    //                co_return;
    //            });

    server.Get("/api/a",
               [](const auto& req, auto& resp,
                  const auto& go) -> boost::asio::awaitable<void> {
                   resp.set_content("<p1>hello,world</p1>", "text/html");
                   co_return;
               });

    server.start();

    Ap.run(1);
    return 0;
}
