
#include <mutex>
#include <cc/asio/condvar.h>
#include <cc/asio/helper.h>
#include <cc/asio/pool.h>
#include <spdlog/spdlog.h>

class AsioDemo {
public:
    AsioDemo() : ap_(cc::AsioPool::get()) {}

    void run() {
        auto& ctx = ap_.get_io_context();
        asio::co_spawn(ctx, task1(), asio::detached);
        asio::co_spawn(ctx, task2(), asio::detached);
        ap_.set_timeout(3000, [this] { cv_.notify_all(); });
        ap_.run();
    }

private:
    asio::task<void> task1() {
        co_await cv_.wait();
        SPDLOG_INFO("task1");
    }

    asio::task<void> task2() {
        auto r = co_await cv_.wait_until(1500);
        SPDLOG_INFO("task2 timeout: {}", r);
    }

private:
    cc::AsioPool& ap_;
    cc::CondVar<std::mutex> cv_;
};

int main() {
    spdlog::set_pattern(R"([%Y-%m-%dT%H:%M:%S.%e] [%^%L%$] [%t] [%s:%#] %v)");

    SPDLOG_INFO("--- begin ---");
    AsioDemo ad;
    ad.run();
    SPDLOG_INFO("---  end  ---");
    return 0;
}
