#include <mutex>
#include <random>
#include <cc/asio.hpp>
#include <fmt/core.h>

/// @return [0, m)
static int random(int m) {
    static std::random_device rd;
    static std::mt19937 gen = std::mt19937(rd());
    std::uniform_int_distribution<> dist(0, m - 1);
    return dist(gen);
}

static asio::awaitable<void> fake_task() {
    int ms = random(3000) + 1000;
    co_return co_await cc::async_sleep(ms);
}

static asio::awaitable<void> fake_thread(int idx, cc::Semaphore<std::mutex>& sem) {
    for (;;) {
        fmt::print(stderr, "[thread#{:02d}] wait...\n", idx);
        co_await sem.acquire();
        fmt::print(stderr, "[thread#{:02d}] run longtime task...\n", idx);
        co_await fake_task();
        fmt::print(stderr, "[thread#{:02d}] task complete...\n", idx);
        sem.release();
    }
}

int main() {
    cc::AsioPool& ap = cc::AsioPool::instance();
    cc::Semaphore<std::mutex> sem(2);

    auto& ctx = ap.get_io_context();
    for (int i = 0; i < 4; i++) {
        asio::co_spawn(ctx, fake_thread(i + 1, sem), asio::detached);
    }

    ap.run();
    return 0;
}
