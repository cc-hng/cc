#pragma once

#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
// pi-lens-ignore: header-not-found
#include <boost/asio/io_context.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/executor_work_guard.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/post.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/dispatch.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/co_spawn.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/steady_timer.hpp>
#include <cc/move_only_function.h>
#include <cc/util.h>
// pi-lens-ignore: header-not-found
#include <fmt/format.h>

namespace cc {

namespace net = boost::asio;

namespace detail {

// Self-sustaining by design: the async_wait handler captures `self`, so the
// timer stays alive until cleared (clearInterval, also callable from inside
// the callback via the passed raw timer) or the io_context is destroyed.
// Dropping the caller's weak handle alone does NOT stop a running interval --
// JS setInterval semantics, intentionally.
class IntervalTimer final : public std::enable_shared_from_this<IntervalTimer> {
public:
    using Callback = MoveOnlyFunction<void(std::shared_ptr<net::steady_timer>)>;

    template <typename ExecutorContext, typename Fn>
    IntervalTimer(ExecutorContext& io_context, std::chrono::milliseconds interval, Fn&& fn)
        : timer_(std::make_shared<net::steady_timer>(io_context)),
          interval_(interval),
          callback_(std::forward<Fn>(fn)) {
        timer_->expires_after(interval_);
    }

    ~IntervalTimer() {}

    void start() {
        using time_point = net::steady_timer::clock_type::time_point;
        if (timer_->expiry() == time_point::max()) {
            return;
        }
        std::shared_ptr<IntervalTimer> self = shared_from_this();
        timer_->expires_after(interval_);
        timer_->async_wait([self](const boost::system::error_code& ec) {
            if (!ec) {
                self->callback_(self->timer_);
                self->start();
            }
        });
    }

    std::weak_ptr<net::steady_timer> getWeakTimer() const { return std::weak_ptr<net::steady_timer>(timer_); }

private:
    std::shared_ptr<net::steady_timer> timer_;
    std::chrono::milliseconds interval_;
    Callback callback_;
};
}  // namespace detail

class AsioPool final {
    using executor_type = net::io_context::executor_type;
    using work_guard_type = net::executor_work_guard<executor_type>;

public:
    using timer_type = std::weak_ptr<net::steady_timer>;

    AsioPool(const AsioPool&) = delete;
    AsioPool& operator=(const AsioPool&) = delete;

public:
    static AsioPool& instance() {
        static AsioPool ap;
        return ap;
    }

    AsioPool() = default;
    ~AsioPool() { shutdown(); }

    inline net::io_context& getIoContext() { return ctx_; }

    template <typename Fn>
    auto setInterval(int ms, Fn&& f) {
        if (ms <= 0) {
            throw std::invalid_argument("interval must be positive");
        }
        std::shared_ptr<detail::IntervalTimer> t =
            std::make_shared<detail::IntervalTimer>(getIoContext(), std::chrono::milliseconds(ms),
                                                    std::forward<Fn>(f));
        t->start();
        return t->getWeakTimer();
    }

    template <typename Fn>
    auto setTimeout(int ms, Fn&& f) {
        // 0 means "fire immediately"; only negative durations are rejected.
        if (ms < 0) {
            throw std::invalid_argument("timeout must be non-negative");
        }
        auto timer = std::make_shared<net::steady_timer>(ctx_);
        timer->expires_after(std::chrono::milliseconds(ms));
        timer->async_wait([fn = std::forward<Fn>(f), timer](boost::system::error_code ec) {
            if (!ec) fn();
        });
        return timer_type(timer);
    }

    inline void clearTimeout(timer_type timer) const {
        if (auto raw = timer.lock()) {
            // cancel() is documented thread-safe, expires_at() is not; dispatch the
            // mutation on the timer's own executor (inline when already on it, e.g.
            // clearInterval called from inside the interval callback, queued from
            // other threads).
            net::dispatch(raw->get_executor(), [raw] {
                raw->cancel();
                raw->expires_at(net::steady_timer::clock_type::time_point::max());
            });
        }
    }

    inline void clearInterval(timer_type timer) const { clearTimeout(timer); }

    void run(int num = std::thread::hardware_concurrency(), bool with_guard = false) {
        if (stopped_.load(std::memory_order_relaxed)) {
            return;
        }
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }

        if (num < 1) {
            num = 1;
        }

        if (with_guard) {
            std::unique_lock _lck{mtx_};
            work_guard_ = std::make_unique<work_guard_type>(ctx_.get_executor());
        }

        std::exception_ptr error;
        std::mutex error_mutex;
        auto run_context = [this, &error, &error_mutex] {
            for (;;) {
                try {
                    ctx_.run();
                    return;
                } catch (...) {
                    std::lock_guard lock(error_mutex);
                    if (!error) {
                        error = std::current_exception();
                    }
                }
            }
        };

        std::vector<std::jthread> threads_;
        threads_.reserve(num - 1);
        for (int i = 0; i < num - 1; i++) {
            threads_.emplace_back(run_context);
        }

        // run on current thread
        run_context();

        for (auto& th : threads_) {
            if (th.joinable()) {
                th.join();
            }
        }

        running_.store(false, std::memory_order_release);
        if (error) {
            std::rethrow_exception(error);
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

    // shutdown is terminal; work submitted afterward may never execute.
    template <typename CompletionToken>
    inline auto post(CompletionToken&& token) {
        return net::post(ctx_, std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    inline auto dispatch(CompletionToken&& token) {
        return net::dispatch(ctx_, std::forward<CompletionToken>(token));
    }

    template <typename Any, typename CompletionToken>
    auto coSpawn(Any&& a, CompletionToken&& token) {
        return net::co_spawn(ctx_, std::forward<Any>(a), std::forward<CompletionToken>(token));
    }

    template <typename Any>
    auto coSpawn(Any&& a) {
        return net::co_spawn(ctx_, std::forward<Any>(a), [](std::exception_ptr e) {
            if (!e) return;
            try {
                std::rethrow_exception(e);
            } catch (const std::exception& ex) {
                fmt::print(stderr, "Error in coSpawn: {}\n", ex.what());
            } catch (...) {
                fmt::print(stderr, "Unknown error in coSpawn\n");
            }
        });
    }

private:
    net::io_context ctx_;
    std::atomic<bool> stopped_{false};
    std::atomic<bool> running_{false};

    // prevent the run() method from return.
    std::mutex mtx_;
    std::unique_ptr<work_guard_type> work_guard_;
};

}  // namespace cc
