#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <boost/asio.hpp>
#include <boost/core/noncopyable.hpp>
#include <cc/util.h>

namespace cc {

namespace asio = boost::asio;

template <typename MutexPolicy = NonMutex, template <class> class WriterLock = LockGuard>
class Semaphore final : public boost::noncopyable {
public:
    Semaphore(std::size_t init_permits) : permits_(init_permits) {}

    asio::awaitable<void> acquire() {
        do {
            WriterLock<MutexPolicy> _lck{mtx_};
            if (permits_ > 0) {
                permits_--;
                co_return;
            }
        } while (0);

        using time_point = asio::steady_timer::clock_type::time_point;
        std::shared_ptr<asio::steady_timer> timer =
            std::make_shared<asio::steady_timer>(co_await asio::this_coro::executor);
        timer->expires_at(time_point::max());
        std::weak_ptr<asio::steady_timer> weak_timer(timer);
        add_handle([weak_timer] {
            if (auto timer = weak_timer.lock()) {
                timer->cancel();
            }
        });
        co_await timer->async_wait(asio::as_tuple(asio::use_awaitable));
    }

    inline void release() {
        WriterLock<MutexPolicy> _lck{mtx_};
        permits_++;

        // 存在等待的
        if (permits_ > 0 && handles_.size() > 0) {
            permits_--;
            auto f = handles_.front();
            handles_.pop_front();
            f();
        }
    }

private:
    template <typename Callback>
    void add_handle(Callback&& cb) {
        WriterLock<MutexPolicy> _lck{mtx_};
        handles_.emplace_back(std::forward<Callback>(cb));
    }

private:
    MutexPolicy mtx_;
    std::size_t permits_;
    std::list<std::function<void()>> handles_;
};

}  // namespace cc
