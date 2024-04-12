#pragma once

#include <functional>
#include <list>
#include <memory>
#include <random>
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/core/noncopyable.hpp>
#include <cc/asio/helper.h>
#include <cc/util.h>

namespace cc {

template <class MutexPolicy = NonMutex, template <class> class WriterLock = LockGuard>
class CondVar final : public boost::noncopyable {
public:
    CondVar()  = default;
    ~CondVar() = default;

    asio::task<void> wait() {
        using time_point = asio::steady_timer::clock_type::time_point;
        auto ctx         = co_await asio::this_coro::executor;
        std::shared_ptr<asio::steady_timer> timer =
            std::make_shared<asio::steady_timer>(ctx);
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

    void notify_all() {
        std::list<std::function<void()>> handles;
        {
            WriterLock<MutexPolicy> _lck{mtx_};
            handles = std::move(handles_);
        }
        for (const auto& fn : handles) {
            fn();
        }
    }

    void notify_one() {
        WriterLock<MutexPolicy> _lck{mtx_};
        int offset = random(handles_.size());
        auto it    = handles_.begin();
        std::advance(it, offset);
        (*it)();
        handles_.erase(it);
    }

private:
    template <typename Callback>
    void wait_impl(Callback&& cb) {
        WriterLock<MutexPolicy> _lck{mtx_};
        handles_.emplace_back(std::forward<Callback>(cb));
    }

    /// @return [0, m)
    static int random(int m) {
        static std::random_device rd;
        static std::mt19937 gen = std::mt19937(rd());
        std::uniform_int_distribution<> dist(0, m - 1);
        return dist(gen);
    }

private:
    MutexPolicy mtx_;
    std::list<std::function<void()>> handles_;
};

}  // namespace cc
