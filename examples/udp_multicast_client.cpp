#include "log.h"
#include <string>
#include <string_view>
#include <cc/asio.hpp>

using boost::asio::ip::address;
using boost::asio::ip::udp;

static auto& g_asp = cc::AsioPool::instance();

int main() {
    init_logger();

    g_asp.co_spawn([]() -> net::task<void> {
        udp::endpoint endpoint(address::from_string("239.255.0.1"), 5656);
        udp::socket socket(co_await net::this_coro::executor, udp::v4());
        std::string msg = "ping";

        //
        for (;;) {
            co_await socket.async_send_to(net::buffer(msg), endpoint, net::use_awaitable);
            co_await cc::async_sleep(1000);
        }
        co_return;
    });

    g_asp.run();
    return 0;
}
