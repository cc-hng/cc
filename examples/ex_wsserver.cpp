#include <cc/asio/core.h>
#include <cc/lit/core.h>
#include <fmt/core.h>

namespace beast = boost::beast;

int main(int argc, char* argv[]) {
    auto& Ap  = cc::AsioPool::get();
    auto& ctx = Ap.get_io_context();

    cc::lit::App server(ctx, "0.0.0.0", 8088);
    fmt::print("Server listen at: *:8088\n");

    server.websocket("/user/:id", [](const auto& req, auto ws) -> asio::awaitable<void> {
        auto id = req.params->at("id");
        fmt::print("id: {}\n", id);

        for (;;) {
            // This buffer will hold the incoming message
            beast::flat_buffer buffer;

            // Read a message
            co_await ws.async_read(buffer);

            // Echo the message back
            ws.text(ws.got_text());
            co_await ws.async_write(buffer.data());
        }
    });

    server.start();
    Ap.run();

    return 0;
}
