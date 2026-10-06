#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>
// pi-lens-ignore: header-not-found
#include <boost/asio/awaitable.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/use_awaitable.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/this_coro.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/steady_timer.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/dispatch.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/as_tuple.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/experimental/awaitable_operators.hpp>
// pi-lens-ignore: header-not-found
#include <boost/system/system_error.hpp>
#include <cc/asio/helper.h>
#include <cc/move_only_function.h>
#include <cc/util.h>

namespace cc {

template <typename MutexPolicy = NonMutex,  //
          template <class> class WriterLock = LockGuard>
class CondVar final {
    MutexPolicy mtx_;
    struct wait_handle_t {
        std::weak_ptr<net::steady_timer> timer;
        MoveOnlyFunction<void()> cancel;
    };

    std::vector<std::shared_ptr<wait_handle_t>> handles_;
    std::size_t generation_ = 0;

public:
    CondVar() { handles_.reserve(4); }
    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;
    ~CondVar() = default;

    net::awaitable<void> wait() { co_await waitSince(generation()); }

    // Snapshot before checking an external predicate, then wait for a newer generation.
    std::size_t generation() {
        WriterLock<MutexPolicy> _lck{mtx_};
        return generation_;
    }

    net::awaitable<void> waitSince(std::size_t generation) {
        using time_point = net::steady_timer::clock_type::time_point;
        auto ctx = co_await net::this_coro::executor;
        std::shared_ptr<net::steady_timer> timer = std::make_shared<net::steady_timer>(ctx);
        timer->expires_at(time_point::max());
        std::weak_ptr<net::steady_timer> weak_timer(timer);
        bool already_notified = false;
        std::shared_ptr<wait_handle_t> handle;
        {
            WriterLock<MutexPolicy> _lck{mtx_};
            already_notified = generation_ != generation;
            if (!already_notified) {
                handle = std::make_shared<wait_handle_t>();
                handle->timer = timer;
                // Wake via expires_at(min) instead of cancel(): in the gap where the
                // handle is published but async_wait is not yet registered, cancel()
                // is a no-op (not sticky); expiry IS sticky -- a past expiry makes the
                // later-registered async_wait fire immediately, closing that gap so
                // a wakeup is never lost. A waiter already registered on the timer
                // completes with operation_aborted; waitSince discriminates that
                // from a real cancellation via the expiry value.
                // Dispatch (not direct mutation): timer expiry is not thread-safe
                // to change from notifier threads while the io thread runs async_wait
                // on the same timer. dispatch runs inline when the notifier is already
                // on that executor (the common case) and queues otherwise.
                handle->cancel = [ctx, weak_timer] {
                    if (auto timer = weak_timer.lock()) {
                        net::dispatch(ctx, [timer] { timer->expires_at(time_point::min()); });
                    }
                };
                handles_.emplace_back(handle);
            }
        }
        if (already_notified) {
            co_return;
        }
        auto [ec] = co_await timer->async_wait(net::as_tuple(net::use_awaitable));
        removeWaitHandle(handle);
        // Propagate cancellation instead of swallowing it: a silent return made
        // loop-based consumers (Semaphore, mpsc, oneshot) re-arm a wait that can
        // never fire again -- leaked timers, and a cancelled waiter stealing
        // values from later receivers. Cancelled awaiters must unwind.
        if (ec) {
            // A notify_* wake sets expires_at(min); waking a PENDING async_wait
            // this way makes Boost complete it with operation_aborted -- the very
            // code a real external cancellation produces. Discriminate by timer
            // state: notify left the expiry at min, a real cancel left it at max.
            if (ec == net::error::operation_aborted && timer->expiry() == time_point::min()) {
                co_return;
            }
            throw boost::system::system_error(ec);
        }
    }

    // Returns true if notified before the timeout expired (std::condition_variable
    // ::wait_until semantics), false if the timeout fired first. `||` completes with
    // index 0 for the left branch (timeout) and 1 for the right (wait).
    net::awaitable<bool> waitUntil(int timeout) {
        using namespace net::experimental::awaitable_operators;
        auto ctx = co_await net::this_coro::executor;
        auto v = co_await (cc::timeout(timeout) || wait());
        co_return v.index() == 1;
    }

    void notifyAll() noexcept {
        std::vector<std::shared_ptr<wait_handle_t>> handles;
        {
            WriterLock<MutexPolicy> _lck{mtx_};
            ++generation_;
            pruneWaitHandles();
            handles = std::move(handles_);
        }
        for (auto& handle : handles) {
            try {
                handle->cancel();
            } catch (...) {
                // ignore
            }
        }
    }

    void notifyOne() noexcept {
        MoveOnlyFunction<void()> f{nullptr};
        {
            WriterLock<MutexPolicy> _lck{mtx_};
            ++generation_;
            pruneWaitHandles();
            if (!handles_.empty()) {
                std::size_t offset = random(handles_.size());
                auto it = handles_.begin();
                std::advance(it, offset);
                f = std::move((*it)->cancel);
                handles_.erase(it);
            }
        }
        try {
            if (f) f();
        } catch (...) {
            // ignore
        }
    }

private:
    void removeWaitHandle(const std::shared_ptr<wait_handle_t>& handle) {
        WriterLock<MutexPolicy> _lck{mtx_};
        std::erase_if(handles_, [&handle](const auto& item) { return item == handle; });
    }

    void pruneWaitHandles() {
        std::erase_if(handles_, [](const auto& handle) { return handle->timer.expired(); });
    }

    /// @return [0, m)
    // Deliberately picks one waiter only; fairness is not part of CondVar contract.
    std::size_t random(std::size_t m) {
        // generation_ is monotonic; std::time has 1s resolution and kept
        // re-picking the same waiter within the same second.
        return generation_ % m;
    }
};

}  // namespace cc

namespace cc::detail {

// One step of the wait protocol shared by the channel/semaphore family:
// `try_claim` runs under the caller's lock and returns an engaged optional once
// it has taken the value (or slot). If it returns nullopt, wait for the next
// CondVar generation and return nullopt so the caller can retry -- the retry
// loop belongs to the call site. The generation snapshot precedes the claim (a
// notify after the snapshot must wake waitSince, never be absorbed by a later
// snapshot), and waitSince()'s cancellation propagation cannot be forgotten
// at a call site.
template <template <class> class Lock, typename Mutex, typename Cv, typename Try>
    requires requires { typename std::invoke_result_t<Try>::value_type; }
net::awaitable<std::optional<typename std::invoke_result_t<Try>::value_type>>  //
wait_claim(Mutex& mtx, Cv& cv, Try try_claim) {
    const auto generation = cv.generation();
    {
        Lock<Mutex> _lck{mtx};
        if (auto claimed = try_claim()) {
            co_return std::move(claimed);
        }
    }
    co_await cv.waitSince(generation);
    co_return std::nullopt;
}

}  // namespace cc::detail
