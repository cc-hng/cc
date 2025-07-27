
#include <cc/asio.hpp>
#include <spdlog/spdlog.h>

namespace mpsc = cc::chan::mpsc;

#define N 3

net::task<void> producer(mpsc::Sender<int> sender) {
    for (int i = 0; i < N; i++) {
        co_await cc::async_sleep(1000);
        SPDLOG_INFO("[producer] send {}", i + 1);
        (*sender)(i + 1);
    }

    sender.reset();
}

net::task<void> consumer(mpsc::Receiver<int> receiver) {
    for (;;) {
        // 模拟长时间任务
        // co_await cc::async_sleep(1500);
        auto item = co_await (*receiver)();
        if (!item.size()) {
            break;
        }

        for (auto i : item) {
            SPDLOG_INFO("[consumer] recv {}", i);
        }
    }

    SPDLOG_INFO("[consumer] over !!!");
}

int main() {
    spdlog::set_pattern(R"([%Y-%m-%dT%H:%M:%S.%e] [%^%L%$] [%t] [%s:%#] %v)");
    auto& ap  = cc::AsioPool::instance();
    auto& ctx = ap.get_io_context();

    SPDLOG_INFO("--- begin ---");
    auto [sender, receiver] = mpsc::make<int>();

    net::co_spawn(ctx, producer(sender), net::detached);
    net::co_spawn(ctx, consumer(std::move(receiver)), net::detached);

    sender.reset();

    ap.run();
    SPDLOG_INFO("---  end  ---");
    return 0;
}
