#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <cc/asio/condvar.h>
#include <cc/asio/helper.h>
#include <cc/move_only_function.h>
#include <cc/util.h>

namespace cc::chan {

namespace detail {
// Default NonMutex: single-threaded; pass std::mutex for cross-thread send.
template <typename T, typename MutexPolicy = NonMutex,  //
          template <class> class Lock = LockGuard>      //
class OneshotContext {
    MutexPolicy mtx_;
    CondVar<MutexPolicy> cv_;
    bool sent_ = false;
    bool received_ = false;
    std::optional<T> val_;

public:
    template <typename Arg>
    void send(Arg&& a) {
        {
            Lock<MutexPolicy> lck{mtx_};
            if (sent_) {
                throw std::runtime_error("Oneshot channel can only send one value!");
            }
            val_.emplace(std::forward<Arg>(a));
            sent_ = true;
        }
        cv_.notifyAll();
    }

    net::awaitable<T> recv() {
        for (;;) {
            auto claimed = co_await cc::detail::wait_claim<Lock>(mtx_, cv_, [this]() -> std::optional<T> {
                if (received_) {
                    throw std::runtime_error("Oneshot channel can only receive one value!");
                }
                if (sent_) {
                    // Only mark received_ when actually handing the value out: marking
                    // it before the wait poisoned the channel (and dereferencing the
                    // empty optional was UB) when the wait got cancelled.
                    received_ = true;
                    return std::move(*val_);
                }
                return std::nullopt;
            });
            if (claimed) {
                co_return std::move(*claimed);
            }
        }
    }
};

}  // namespace detail

template <typename T, typename MutexPolicy = NonMutex>
auto make() -> std::unique_ptr<detail::OneshotContext<T, MutexPolicy>> {
    return std::make_unique<detail::OneshotContext<T, MutexPolicy>>();
}

}  // namespace cc::chan
