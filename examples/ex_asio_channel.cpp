
#include <cc/asio/core.h>
#include <spdlog/spdlog.h>

#define N 10

asio::task<void> producer(auto sender) {
    for (int i = 0; i < N; i++) {
        co_await cc::async_sleep(1000);
        SPDLOG_INFO("[producer] send {}", i + 1);
        (*sender)(i + 1);
    }
}

asio::task<void> consumer(auto receiver) {
    for (;;) {
        // 模拟长时间任务
        co_await cc::async_sleep(1500);
        std::deque<int> list = co_await (*receiver)();
        for (const auto& v : list) {
            SPDLOG_INFO("[consumer] recv {}", v);
        }
    }
}

int main() {
    spdlog::set_pattern(R"([%Y-%m-%dT%H:%M:%S.%e] [%^%L%$] [%t] [%s:%#] %v)");
    auto& ap  = cc::AsioPool::get();
    auto& ctx = ap.get_io_context();

    SPDLOG_INFO("--- begin ---");
    auto [sender, receiver] = cc::chan::make_mpsc<int>();

    asio::co_spawn(ctx, producer(sender), asio::detached);
    asio::co_spawn(ctx, consumer(std::move(receiver)), asio::detached);

    ap.run();
    SPDLOG_INFO("---  end  ---");
    return 0;
}
