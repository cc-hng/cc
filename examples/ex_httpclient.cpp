#include <iostream>
#include <cc/asio.hpp>
#include <cc/lit/client.h>
#include <gsl/gsl>

int main() {
    auto defer = gsl::finally([] { cc::lit::fetch_pool_release(); });
    auto& ap   = cc::AsioPool::instance();

    ap.co_spawn([]() -> asio::awaitable<void> {
        cc::lit::fetch_option_t opt = {.keepalive = true};
        auto resp = co_await cc::lit::fetch("http://127.0.0.1:8088", opt);
        std::cout << resp.body() << std::endl;

        std::cout << "------------------- 分割线 -------------------" << std::endl;
        resp = co_await cc::lit::fetch("http://127.0.0.1:8088/api/a", opt);
        std::cout << resp.body() << std::endl;

        std::cout << "------------------- 分割线 -------------------" << std::endl;
        resp = co_await cc::lit::fetch("http://127.0.0.1:8088/api/param?a=1&b=2", opt);
        std::cout << resp.body() << std::endl;

        std::cout << "------------------- 分割线 -------------------" << std::endl;
        opt.method = boost::beast::http::verb::post;
        resp       = co_await cc::lit::fetch("http://127.0.0.1:8088/api/c", opt);
        std::cout << resp.body() << std::endl;
    });

    ap.run(1);
    return 0;
}
