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
#include <stdio.h>

namespace cc {

namespace detail {

class IntervalTimer final : public std::enable_shared_from_this<IntervalTimer> {
public:
    using Callback = std::function<void(std::shared_ptr<boost::asio::steady_timer>)>;

    IntervalTimer(boost::asio::io_context& io_context, std::chrono::milliseconds interval,
                  Callback fn)
      : timer_(std::make_shared<boost::asio::steady_timer>(io_context))
      , interval_(interval)
      , callback_((Callback&&)fn) {}

    ~IntervalTimer() {}

    void start() {
        std::shared_ptr<IntervalTimer> self = shared_from_this();
        timer_->expires_after(interval_);
        timer_->async_wait([self](const boost::system::error_code& ec) {
            if (!ec) {
                self->callback_(self->timer_);
                self->start();
            }
        });
    }

    std::weak_ptr<boost::asio::steady_timer> get_weak_timer() const {
        return std::weak_ptr<boost::asio::steady_timer>(timer_);
    }

private:
    std::shared_ptr<boost::asio::steady_timer> timer_;
    std::chrono::milliseconds interval_;
    Callback callback_;
};
}  // namespace detail

class AsioPool final : boost::noncopyable {
    using executor_t   = boost::asio::io_context::executor_type;
    using work_guard_t = boost::asio::executor_work_guard<executor_t>;

public:
    static AsioPool& instance() {
        static AsioPool ap;
        return ap;
    }

    explicit AsioPool() : stopped_(false), work_guard_(nullptr) {}

    ~AsioPool() { shutdown(); }

    inline boost::asio::io_context& get_io_context() { return ctx_; }

    template <typename CompletionToken>
    inline auto enqueue(CompletionToken&& token) {
        return boost::asio::dispatch(ctx_, std::forward<CompletionToken>(token));
    }

    template <typename Fn>
    auto set_interval(int ms, Fn&& f) {
        std::shared_ptr<detail::IntervalTimer> t =
            std::make_shared<detail::IntervalTimer>(get_io_context(), std::chrono::milliseconds(ms),
                                                    std::forward<Fn>(f));
        t->start();
        return t->get_weak_timer();
    }

    template <typename Fn>
    auto set_timeout(int ms, Fn&& f) {
        auto timer = std::make_shared<boost::asio::steady_timer>(ctx_);
        timer->expires_after(std::chrono::milliseconds(ms));
        std::function handle = [fn = std::forward<Fn>(f), timer](boost::system::error_code ec) {
            if (!ec) {
                fn();
            }
        };
        timer->async_wait(handle);
        return std::weak_ptr<boost::asio::steady_timer>(timer);
    }

    inline void clear_timeout(std::weak_ptr<boost::asio::steady_timer> timer) const {
        if (auto raw = timer.lock()) {
            raw->cancel();
        }
    }

    inline void clear_interval(std::weak_ptr<boost::asio::steady_timer> timer) const {
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

#ifdef CC_ENABLE_COROUTINE
    template <typename Any, typename CompletionToken>
    auto co_spawn(Any&& a, CompletionToken&& token) {
        return boost::asio::co_spawn(ctx_, std::forward<Any>(a),
                                     std::forward<CompletionToken>(token));
    }

    template <typename Any>
    auto co_spawn(Any&& a) {
        return boost::asio::co_spawn(ctx_, std::forward<Any>(a), [](std::exception_ptr e) {
            if (!e) return;
            try {
                std::rethrow_exception(e);
            } catch (std::exception& e) {
                fprintf(stderr, "Error in co_spawn: %s\n", e.what());
            }
        });
    }
#endif

private:
    boost::asio::io_context ctx_;
    std::atomic<bool> stopped_;

    // prevent the run() method from return.
    std::mutex mtx_;
    std::unique_ptr<work_guard_t> work_guard_;
};

}  // namespace cc
