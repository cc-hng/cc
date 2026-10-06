#include "stdafx.h"

void init_logger() {
    LogBuilder()                                 //
        .addConsoleLogger(spdlog::level::debug)  //
        .build();
    LOGI("logger init ok.");
}

/// 等待协程：co_await sem.acquire() 获取许可
net::awaitable<void> waiter(cc::Semaphore<>& sem, int id) {
    LOGI("W{} waiting...", id);
    co_await sem.acquire();
    LOGI("W{} acquired", id);
}

net::awaitable<void>  //
async_main() {
    // 场景1：sem(1)，W1 立即获取不阻塞，W2 阻塞后由 release 唤醒
    {
        cc::Semaphore<> sem(1);
        CO_SPAWN("W1", waiter(sem, 1));
        CO_SPAWN("W2", waiter(sem, 2));
        co_await cc::timeout(50);  // 确保 W1 已获取、W2 已挂起
        LOGI("[scene1] release");
        sem.release();  // 唤醒 W2
        co_await cc::timeout(50);
    }

    // 场景2：sem(0)，3 个等待者按序挂起，逐个 release 验证 FIFO 唤醒
    {
        cc::Semaphore<> sem(0);
        CO_SPAWN("W1", waiter(sem, 1));
        CO_SPAWN("W2", waiter(sem, 2));
        CO_SPAWN("W3", waiter(sem, 3));
        co_await cc::timeout(50);  // 确保按 spawn 顺序挂起
        LOGI("[scene2] release 1");
        sem.release();  // 唤醒 W1（队首）
        co_await cc::timeout(50);
        LOGI("[scene2] release 2");
        sem.release();  // 唤醒 W2
        co_await cc::timeout(50);
        LOGI("[scene2] release 3");
        sem.release();  // 唤醒 W3
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
