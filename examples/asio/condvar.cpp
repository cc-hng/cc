#include "stdafx.h"

void init_logger() {
    LogBuilder()                                 //
        .addConsoleLogger(spdlog::level::debug)  //
        .build();
    LOGI("logger init ok.");
}

/// 等待协程：co_await cv.wait() 直到被 notify 唤醒
net::awaitable<void> waiter(cc::CondVar<>& cv, int id) {
    LOGI("W{} waiting...", id);
    co_await cv.wait();
    LOGI("W{} woken", id);
}

/// 超时等待协程：co_await cv.waitUntil(ms)
/// 返回 true 表示超时未被唤醒，false 表示被 notify 唤醒
net::awaitable<void> waiter_until(cc::CondVar<>& cv, int id, int ms) {
    LOGI("W{} waitUntil({}ms)...", id, ms);
    bool timed_out = co_await cv.waitUntil(ms);
    LOGI("W{} waitUntil returned timed_out={}", id, timed_out);
}

net::awaitable<void>  //
async_main() {
    // 场景1：wait() + notifyAll()，3 个等待者全部被唤醒
    {
        cc::CondVar<> cv;
        CO_SPAWN("W1", waiter(cv, 1));
        CO_SPAWN("W2", waiter(cv, 2));
        CO_SPAWN("W3", waiter(cv, 3));
        co_await cc::timeout(50);  // 确保三个等待者均已挂起在 wait()
        LOGI("[scene1] notifyAll");
        cv.notifyAll();
        co_await cc::timeout(50);  // 等待唤醒完成
    }

    // 场景2：wait() + notifyOne()，每次只随机唤醒一个
    {
        cc::CondVar<> cv;
        CO_SPAWN("W1", waiter(cv, 1));
        CO_SPAWN("W2", waiter(cv, 2));
        co_await cc::timeout(50);
        LOGI("[scene2] notifyOne (1st)");
        cv.notifyOne();
        co_await cc::timeout(50);
        LOGI("[scene2] notifyOne (2nd)");
        cv.notifyOne();
        co_await cc::timeout(50);
    }

    // 场景3：waitUntil() 超时返回 true（无人 notify）
    {
        cc::CondVar<> cv;
        CO_SPAWN("W1", waiter_until(cv, 1, 100));
        co_await cc::timeout(200);  // > 100ms，等待超时发生
    }

    // 场景4：waitUntil() 被 notify 唤醒返回 false
    {
        cc::CondVar<> cv;
        CO_SPAWN("W1", waiter_until(cv, 1, 1000));
        co_await cc::timeout(50);  // 确保等待者已挂起
        LOGI("[scene4] notifyAll");
        cv.notifyAll();
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
