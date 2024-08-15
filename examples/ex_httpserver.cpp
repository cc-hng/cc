
#include <boost/callable_traits.hpp>
#include <boost/hana.hpp>
#include <cc/asio.hpp>
#include <cc/json.h>
#include <cc/lit.hpp>
#include <cc/lit/multipart_parser.h>
#include <cc/lit/util.h>
#include <cc/type_traits.h>
#include <fmt/core.h>
#include <msg/msg.h>

class HttpServer {
public:
    HttpServer(boost::asio::io_context& ctx, std::string_view ip, int port)
      : app_(ctx, ip, port)
      , origin_(10) {}

    void start() {
        app_.serve_static("/public", "/data/www/html");

        app_.Get("/api/a",
                 [](const auto& req, auto& resp, const auto& go) -> boost::asio::awaitable<void> {
                     co_await cc::async_sleep(100);
                     co_return resp.set_content("<p1>hello,world</p1>", "text/html");
                 });

        app_.Any("/api/b", [](const auto& req, auto& resp) {
            resp.set_content("<p1>hello,world2</p1>", "text/html");
        });

        app_.Post("/api/c", [](const auto& req, auto& resp) -> boost::asio::awaitable<void> {
            co_return resp.set_content("<p1>hello,world3</p1>", "text/html");
        });

        app_.Post("/api/e", cc::lit::make_route(&HttpServer::api_e, this));
        app_.Post("/api/f", cc::lit::make_route(&HttpServer::api_f, this));

        app_.Get("/api/param", [](const auto& req, auto& resp) {
            if (req.querys) {
                resp.set_content(cc::json::dump(*req.querys));
            }
        });

        app_.Post("/api/upload", [](const cc::lit::http_request_t& req, auto& resp) {
            auto result = cc::lit::multipart_formdata_parse(req);
            for (const auto& f : result) {
                fmt::print("name:{}, filename:{}, content_type:{}\n", f.name, f.filename,
                           f.content_type);
                fmt::print("content:{}\n", f.content);
                fmt::print("---------------------------------------------------------\n");
            }
            resp.set_content(R"({"code":200,"data":{}})");
        });

        app_.Get("/api/:service/:method", [](const auto& req, auto& resp) {
            auto msg = cc::json::dump(*req.params);
            resp.set_content(msg);
        });

        app_.Get("/v1/:abc:", [](const auto& req, auto& resp) {
            resp.set_content(cc::json::dump(*req.params));
        });

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
    auto& Ap  = cc::AsioPool::instance();
    auto& ctx = Ap.get_io_context();

    HttpServer server(ctx, "0.0.0.0", 8088);
    fmt::print("Server listen at: *:8088\n");

    server.start();

    Ap.run(1);
    return 0;
}
