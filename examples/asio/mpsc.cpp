#include "stdafx.h"
#include <fmt/ranges.h>

void init_logger() {
    LogBuilder()                                 //
        .addConsoleLogger(spdlog::level::debug)  //
        .build();
    LOGI("logger init ok.");
}

/// 消费者协程：recv 一次，打印批量接收结果
net::awaitable<void> recv_once(cc::chan::Receiver<int> receiver, int id) {
    LOGI("C{} recv...", id);
    auto vals = co_await (*receiver)();
    LOGI("C{} recv {} items: [{}]", id, vals.size(), fmt::join(vals, ","));
}

net::awaitable<void>  //
async_main() {
    // 场景1：单生产者 send 3 个，recv 一次批量接收
    {
        auto [sender, receiver] = cc::chan::make_mpsc<int>();
        (*sender)(1);
        (*sender)(2);
        (*sender)(3);
        co_await cc::timeout(10);  // 确保已入队
        auto vals = co_await (*receiver)();
        LOGI("[scene1] recv {} items: [{}]", vals.size(), fmt::join(vals, ","));
    }

    // 场景2：recv 先阻塞，send 唤醒
    {
        auto [sender, receiver] = cc::chan::make_mpsc<int>();
        CO_SPAWN("C1", recv_once(std::move(receiver), 1));
        co_await cc::timeout(50);  // 确保 C1 已阻塞
        LOGI("[scene2] send 42");
        (*sender)(42);
        co_await cc::timeout(50);
    }

    LOGI("all scenes done.");
}

int main() {
    init_logger();
    CO_SPAWN("main", async_main());
    g_asp.run(1);
    return 0;
}
