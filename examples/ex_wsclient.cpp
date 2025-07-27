
#include "log.h"
#include <chrono>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <cc/asio.hpp>

namespace beast = boost::beast;

static auto& g_asp = cc::AsioPool::instance();

// clang-format off
net::task<void>  // clang-format on
do_session(std::string host, int port) {
    auto ctx      = co_await net::this_coro::executor;
    auto resolver = net::ip::tcp::resolver{ctx};
    auto stream   = beast::websocket::stream<beast::tcp_stream>{ctx};

    auto const results = co_await resolver.async_resolve(host, std::to_string(port));
    beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));

    auto ep = co_await beast::get_lowest_layer(stream).async_connect(results);

    // Update the host_ string. This will provide the value of the
    // Host HTTP header during the WebSocket handshake.
    // See https://tools.ietf.org/html/rfc7230#section-5.4
    host += ':' + std::to_string(ep.port());

    // Turn off the timeout on the tcp_stream, because
    // the websocket stream has its own timeout system.
    beast::get_lowest_layer(stream).expires_never();

    // Set suggested timeout settings for the websocket
    stream.set_option(beast::websocket::stream_base::timeout::suggested(beast::role_type::client));

    // Set a decorator to change the User-Agent of the handshake
    stream.set_option(
        beast::websocket::stream_base::decorator([](beast::websocket::request_type& req) {
            req.set(beast::http::field::user_agent,
                    std::string(BOOST_BEAST_VERSION_STRING) + " websocket-client-coro");
        }));

    // Perform the websocket handshake
    co_await stream.async_handshake(host, "/echo");

    for (;;) {
        LOGI("send: ping");
        // Send the message
        co_await stream.async_write(net::buffer("ping"));

        // This buffer will hold the incoming message
        beast::flat_buffer buffer;

        // Read a message into our buffer
        co_await stream.async_read(buffer);

        // The make_printable() function helps print a ConstBufferSequence
        LOGI("recv: {}",
             std::string(static_cast<const char*>(buffer.data().data()), buffer.data().size()));

        co_await cc::async_sleep(1000);
    }

    // Close the WebSocket connection
    co_await stream.async_close(beast::websocket::close_code::normal);

    // If we get here then the connection is closed gracefully
}

int main() {
    init_logger();

    g_asp.co_spawn(do_session("127.0.0.1", 8088));

    g_asp.run();
    return 0;
}
