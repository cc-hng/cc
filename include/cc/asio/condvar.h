#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/core/noncopyable.hpp>
#include <cc/asio/helper.h>
#include <cc/util.h>
#include <time.h>

namespace cc {

// clang-format off
template <
    typename MutexPolicy = NonMutex,
    template <class> class WriterLock = LockGuard
>  // clang-format on
class CondVar final : public boost::noncopyable {
    MutexPolicy mtx_;
    std::vector<std::function<void()>> handles_;

public:
    CondVar() { handles_.reserve(4); }
    ~CondVar() = default;

    asio::task<void> wait() {
        using time_point                          = asio::steady_timer::clock_type::time_point;
        auto ctx                                  = co_await asio::this_coro::executor;
        std::shared_ptr<asio::steady_timer> timer = std::make_shared<asio::steady_timer>(ctx);
        timer->expires_at(time_point::max());
        std::weak_ptr<asio::steady_timer> weak_timer(timer);
        wait_impl([weak_timer] {
            if (auto timer = weak_timer.lock()) {
                timer->cancel();
            }
        });
        co_await timer->async_wait(asio::as_tuple(asio::use_awaitable));
    }

    asio::task<bool> wait_until(int timeout) {
        using namespace asio::experimental::awaitable_operators;
        auto ctx = co_await asio::this_coro::executor;
        auto v   = co_await (cc::async_sleep(timeout) || wait());
        co_return v.index() == 0;
    }

    void notify_all() noexcept {
        std::vector<std::function<void()>> handles;
        {
            WriterLock<MutexPolicy> _lck{mtx_};
            handles = std::move(handles_);
        }
        for (const auto& fn : handles) {
            try {
                fn();
            } catch (...) {
                // ignore
            }
        }
    }

    void notify_one() noexcept {
        std::function<void()> f = nullptr;
        do {
            WriterLock<MutexPolicy> _lck{mtx_};
            if (handles_.size() > 0) {
                int offset = random(handles_.size());
                auto it    = handles_.begin();
                std::advance(it, offset);
                f = std::move(*it);
                handles_.erase(it);
            }
        } while (0);
        try {
            if (f) f();
        } catch (...) {
            // ignore
        }
    }

private:
    template <typename Callback>
    void wait_impl(Callback&& cb) {
        WriterLock<MutexPolicy> _lck{mtx_};
        handles_.emplace_back(std::forward<Callback>(cb));
    }

    /// @return [0, m)
    static inline int random(int m) { return time(NULL) / m; }
};

}  // namespace cc
