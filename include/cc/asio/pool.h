#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include <boost/asio.hpp>
#include <boost/core/noncopyable.hpp>
#include <stddef.h>

namespace cc {

class AsioPool final : boost::noncopyable {
    using executor_t   = boost::asio::io_context::executor_type;
    using work_guard_t = boost::asio::executor_work_guard<executor_t>;

public:
    static AsioPool& get() {
        static AsioPool ap;
        return ap;
    }

    explicit AsioPool() : stopped_(false), work_guard_(nullptr) {}

    ~AsioPool() { shutdown(); }

    inline boost::asio::io_context& get_io_context() { return ctx_; }

    template <typename CompletionToken>
    auto enqueue(CompletionToken&& token) {
        return boost::asio::dispatch(ctx_, std::forward<CompletionToken>(token));
    }

    template <typename Fn>
    auto set_timeout(int ms, Fn&& f) {
        auto timer = std::make_shared<boost::asio::steady_timer>(
            ctx_, std::chrono::milliseconds(ms));
        timer->async_wait(
            [timer, fn = std::forward<Fn>(f)](boost::system::error_code ec) {
                if (!ec) {
                    fn();
                }
            });
        return std::weak_ptr<boost::asio::steady_timer>(timer);
    }

    void clear_timeout(std::weak_ptr<boost::asio::steady_timer> timer) const {
        if (auto raw = timer.lock()) {
            raw->cancel();
        }
    }

    void clear_interval(std::weak_ptr<boost::asio::steady_timer> timer) const {
        clear_timeout(timer);
    }

    void run(int num = std::thread::hardware_concurrency(), bool with_guard = false) {
        if (stopped_.load(std::memory_order_relaxed)) {
            return;
        }

        if (with_guard) {
            std::unique_lock _lck{mtx_};
            work_guard_ = std::make_unique<work_guard_t>(ctx_.get_executor());
        }

        std::vector<std::thread> threads_;
        threads_.reserve(num - 1);
        for (size_t i = 0; i < num - 1; i++) {
            threads_.emplace_back([&] { ctx_.run(); });
        }

        // run on current thread
        ctx_.run();

        for (auto& th : threads_) {
            if (th.joinable()) {
                th.join();
            }
        }
    }

    void shutdown() {
        if (!stopped_.exchange(true, std::memory_order_acquire)) {
            ctx_.stop();
            std::unique_lock _lck{mtx_};
            if (work_guard_) {
                work_guard_.reset();
            }
        }
    }

private:
    boost::asio::io_context ctx_;
    std::atomic<bool> stopped_;

    // prevent the run() method from return.
    std::mutex mtx_;
    std::unique_ptr<work_guard_t> work_guard_;
};

}  // namespace cc
