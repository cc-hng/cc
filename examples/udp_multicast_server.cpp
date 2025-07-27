#include "log.h"
#include <string>
#include <string_view>
#include <cc/asio.hpp>
#include <gsl/gsl>

using boost::asio::ip::udp;

static auto& g_asp = cc::AsioPool::instance();

class UdpRecver {
    std::string ip_;
    int port_;
    udp::socket* sock_;

public:
    UdpRecver(std::string_view ip, int port) : ip_(ip), port_(port), sock_(nullptr) {
        LOGI("UdpRecver()");
    }

    ~UdpRecver() {
        shutdown();
        LOGI("~UdpRecver()");
    }

    void shutdown() {
        if (sock_) {
            sock_->close();
            sock_ = nullptr;
        }
    }

    net::task<void> operator()() {
        std::array<char, 64> data;
        udp::endpoint listen_endpoint(net::ip::make_address("0.0.0.0"), port_);
        udp::socket socket(co_await net::this_coro::executor);

        socket.open(listen_endpoint.protocol());
        socket.set_option(udp::socket::reuse_address(true));
        socket.bind(listen_endpoint);
        socket.set_option(net::ip::multicast::join_group(net::ip::make_address(ip_)));
        sock_ = &socket;

        udp::endpoint sender_endpoint;
        for (;;) {
            try {
                auto nbytes = co_await socket.async_receive_from(net::buffer(data), sender_endpoint,
                                                                 net::use_awaitable);
                LOGI("recv: {}", std::string_view((const char*)(&*data.begin()), nbytes));
            } catch (const boost::system::system_error& e) {
                auto ec = e.code();
                if (GSL_LIKELY(ec == net::error::operation_aborted
                               || ec == net::error::bad_descriptor)) {
                    throw;
                }
            }
        }
    }
};

int main() {
    init_logger();

    auto udp_recver = std::make_shared<UdpRecver>("239.255.0.1", 5656);
    g_asp.co_spawn((*udp_recver)());

    g_asp.set_timeout(3000, [udp_recver] {
        udp_recver->shutdown();
        LOGW("stop...");
    });

    g_asp.run();
    return 0;
}
