#include <boost/asio/experimental/awaitable_operators.hpp>
#include <cc/asio/helper.h>
#include <cc/asio/pool.h>
#include <cc/stopwatch.h>
#include <spdlog/spdlog.h>

using namespace net::experimental::awaitable_operators;
using namespace std::chrono_literals;

static auto& Ap = cc::AsioPool::instance();

net::task<int> task_1() {
    co_await cc::async_sleep(300);
    SPDLOG_INFO("task_1");
    co_return 3;
}

net::task<int> task_2() {
    co_await cc::async_sleep(600);
    SPDLOG_INFO("task_2");
    co_return 6;
}

net::task<int> task_3() {
    co_await cc::async_sleep(900);
    SPDLOG_INFO("task_3");
    co_return 9;
}

net::task<void> run_all() {
    cc::StopWatch sw;
    auto [one, two, three] = co_await (task_1() && task_2() && task_3());
    SPDLOG_INFO("run_all: cost: {}s, result: ({}, {}, {})", sw.elapsed(), one, two, three);
}

net::task<void> run_any() {
    cc::StopWatch sw;
    auto v = co_await (task_1() || task_2() || task_3());  // NOLINT
    SPDLOG_INFO("run_any: cost: {}s, result: {}", sw.elapsed(), v.index());
}

net::task<void> tick() {
    cc::StopWatch stopwatch;
    for (int i = 0; i < 60; i++) {
        co_await cc::async_sleep(20);
        // std::cout << "["  "] tick " << i + 1
        //           << ", cost: " << stopwatch.elapsed() << "s" << std::endl;
    }

    SPDLOG_INFO("tick shutdown");
    Ap.shutdown();
}

net::task<void> tick2() {
    for (;;) {
        co_await cc::async_sleep(500);
        co_await Ap.enqueue(net::use_awaitable);
        SPDLOG_INFO("tick2");
    }
}

int main() {
    spdlog::set_pattern(R"([%Y-%m-%dT%H:%M:%S.%e] [%^%L%$] [%t] [%s:%#] %v)");

    SPDLOG_INFO("-------- start --------");
    cc::StopWatch stopwatch;
    auto& ctx = Ap.get_io_context();

    net::co_spawn(ctx, run_all(), net::detached);
    net::co_spawn(ctx, run_any(), net::detached);
    // net::co_spawn(ctx, tick(), net::detached);
    // net::co_spawn(ctx, tick2(), net::detached);

    // stop a coroutine
    net::cancellation_signal sig;
    net::co_spawn(ctx, tick2(), net::bind_cancellation_slot(sig.slot(), net::detached));
    Ap.set_timeout(400, [&] {
        SPDLOG_INFO("Stop tick2 coroutine.");
        sig.emit(net::cancellation_type::all);
    });

    // schedule
    net::thread_pool thpool(1);
    net::post(thpool.get_executor(), []() { SPDLOG_INFO("run on thread pool"); });
    net::co_spawn(ctx, cc::schedule(thpool, [] { SPDLOG_INFO("Schedule on io_context."); }),
                  net::detached);
    net::co_spawn(thpool,
                  cc::schedule(ctx,
                               []() -> net::task<void> {
                                   SPDLOG_INFO("Schedule on io_context(awaitable).");
                                   co_return;
                               }),
                  net::detached);

    Ap.run(1);
    SPDLOG_INFO("main cost: {}s", stopwatch.elapsed());
    return 0;
}
