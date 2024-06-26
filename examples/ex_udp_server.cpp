#include <array>
#include <iostream>
#include <string_view>
#include <cc/asio/core.h>

class UdpServer {
public:
    UdpServer(boost::asio::io_context& io_context, std::string_view ip, short port)
      : sock_(io_context,
              boost::asio::ip::udp::endpoint(boost::asio::ip::make_address_v4(ip), port)) {
        boost::asio::co_spawn(io_context, run(), [](std::exception_ptr e) {
            if (e) {
                try {
                    std::rethrow_exception(e);
                } catch (std::exception& e) {
                    std::cerr << "UdpServer Error: " << e.what() << "\n";
                }
            }
        });
    }

private:
    boost::asio::awaitable<void> run() {
        while (true) {
            boost::asio::ip::udp::endpoint endpoint;
            auto bytes = co_await sock_.async_receive_from(boost::asio::buffer(buffer_),
                                                           endpoint,
                                                           boost::asio::use_awaitable);
            co_await sock_.async_send_to(boost::asio::buffer(buffer_, bytes), endpoint,
                                         boost::asio::use_awaitable);
        }
        co_return;
    }

private:
    boost::asio::ip::udp::socket sock_;
    std::array<char, 64 * 1024> buffer_;
};

int main() {
    auto& pool = cc::AsioPool::instance();
    UdpServer server(pool.get_io_context(), "127.0.0.1", 3000);
    pool.run(1);
    return 0;
}
