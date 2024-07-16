#include <iostream>
#include <cc/asio/pool.h>
#include <cc/lit/client.h>

namespace net = boost::asio;

int main() {
    auto& ioc = cc::AsioPool::instance().get_io_context();

    net::co_spawn(
        ioc,
        []() -> net::awaitable<void> {
            auto resp = co_await cc::lit::fetch("http://example.com");
            std::cout << "响应: " << resp.body() << std::endl;
        },
        net::detached);

    ioc.run();

    cc::lit::fetch_pool_release();
    return 0;
}
