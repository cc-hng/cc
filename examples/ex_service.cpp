#include "log.h"
#include <cc/asio.hpp>
#include <cc/service.h>
#include <cc/signal.h>

static auto& g_asp = cc::AsioPool::instance();
int add(int a, int b) {
    return a + b;
}

asio::task<void> task1() {
    for (int i = 0;; i++) {
        LOGI("task1: {}", i + 1);
        co_await cc::async_sleep(1000);
    }
}

class Algo {
public:
    int add(int a, int b) { return a + b; }
};

int main(int argc, char* argv[]) {
    init_logger();

    auto& g_svc = cc::ConcurrentService::instance();

    g_svc.advertise("/add", add);

    auto sum = g_svc.call<int, int, int>("/add", 1, 2);
    LOGI("sum = {}", sum);

    g_asp.co_spawn([]() -> asio::task<void> {
        auto& g_svc = cc::ConcurrentService::instance();
        for (;;) {
            auto sum = co_await g_svc.co_call<int>("/add", 1, 2);
            LOGI("sum1 = {}", sum);

            const int a = 3;
            const int b = 4;
            sum         = co_await g_svc.co_call<int>("/add", a, b);
            LOGI("sum2 = {}", sum);
            co_await cc::async_sleep(1000);
        }
    });

    g_asp.set_interval(3000, [](auto timerid) { LOGI("interval..."); });

    g_asp.run();
    return 0;
}
