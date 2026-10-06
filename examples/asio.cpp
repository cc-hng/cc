
#include "stdafx.h"

void init_logger() {
    LogBuilder()                                 //
        .addConsoleLogger(spdlog::level::debug)  //
        .build();
    LOGI("logger init ok.");
}

net::awaitable<void> async_main();

int main() {
    init_logger();
    CO_SPAWN("main", async_main());
    g_asp.run(1);
    return 0;
}

net::awaitable<void>  //
async_main() {
    for (int i = 0; i < 3; ++i) {
        co_await cc::timeout(100);
        LOGI("tick {}...", i + 1);
    }
}
