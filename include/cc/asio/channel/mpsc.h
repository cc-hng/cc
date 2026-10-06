#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <cc/asio/condvar.h>
#include <cc/asio/helper.h>
#include <cc/move_only_function.h>
#include <cc/util.h>
#include <gsl/gsl>

namespace cc::chan {

namespace detail {
template <typename T, typename MutexPolicy = NonMutex, template <class> class Lock = LockGuard>
class MpscContext final {
    MutexPolicy mtx_;
    CondVar<MutexPolicy> cv_;
    std::deque<T> queue_;
    bool stopped_ = false;

public:
    MpscContext() = default;
    ~MpscContext() = default;

    void close() {
        {
            Lock<MutexPolicy> lck{mtx_};
            stopped_ = true;
        }
        cv_.notifyAll();
    }

    template <typename Arg>
    void send(Arg&& a) {
        {
            Lock<MutexPolicy> lck{mtx_};
            if (stopped_) {
                throw std::runtime_error("send() on a closed channel");
            }
            queue_.emplace_back(std::forward<Arg>(a));
        }
        cv_.notifyAll();
    }

    net::awaitable<std::deque<T>>  //
    recv() {
        for (;;) {
            auto claimed =
                co_await cc::detail::wait_claim<Lock>(mtx_, cv_, [this]() -> std::optional<std::deque<T>> {
                    // Drain pending data first: close() must not discard values sent
                    // before it.
                    if (!queue_.empty()) {
                        // swap, not move: a moved-from container is valid but
                        // unspecified, so the empty() check above could see leftovers.
                        std::deque<T> out;
                        out.swap(queue_);
                        return out;
                    }
                    if (stopped_) {
                        return std::deque<T>{};
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

template <typename T>
// Sender accepts const T&, so queued values must be copyable.
using Sender = std::shared_ptr<MoveOnlyFunction<void(const T&)>>;

template <typename T>
using Receiver = std::unique_ptr<MoveOnlyFunction<net::awaitable<std::deque<T>>()>>;

template <typename T, typename MutexPolicy = NonMutex>
auto make_mpsc() -> std::tuple<Sender<T>, Receiver<T>> {
    using ctx_type = detail::MpscContext<T, MutexPolicy>;
    auto ctx = std::make_shared<ctx_type>();
    auto defer = std::make_shared<gsl::final_action<MoveOnlyFunction<void()>>>([ctx] { ctx->close(); });
    auto snder = std::make_shared<typename Sender<T>::element_type>(
        [ctx, defer](auto&& a) { ctx->send(std::forward<decltype(a)>(a)); });
    auto recver = std::make_unique<typename Receiver<T>::element_type>(
        [ctx]() -> net::awaitable<std::deque<T>> { co_return co_await ctx->recv(); });
    return std::make_tuple(std::move(snder), std::move(recver));
}

}  // namespace cc::chan
