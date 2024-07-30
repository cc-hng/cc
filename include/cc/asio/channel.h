#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <cc/asio/condvar.h>
#include <gsl/gsl>

namespace cc {
namespace chan {

namespace detail {
template <typename T, typename MutexPolicy = NonMutex, template <class> class Lock = LockGuard>
struct mpsc_context_t {
    MutexPolicy mtx_;
    CondVar<MutexPolicy> cv_;
    std::deque<T> queue_;
    bool stopped_ = false;

    template <typename A>
    void send(A&& a) noexcept {
        do {
            Lock<MutexPolicy> _lck{mtx_};
            queue_.emplace_back(std::forward<A>(a));
        } while (0);
        cv_.notify_all();
    }

    void close() noexcept {
        Lock<MutexPolicy> _lck{mtx_};
        stopped_ = true;
        cv_.notify_all();
    }

    std::optional<T> pop_front() {
        Lock<MutexPolicy> _lck{mtx_};
        if (!queue_.empty()) {
            auto front = queue_.front();
            queue_.pop_front();
            return front;
        } else {
            return std::nullopt;
        }
    }

    asio::awaitable<std::optional<T>> recv() {
        auto front = pop_front();
        if (front.has_value()) {
            co_return front;
        }

        if (1) {
            Lock<MutexPolicy> _lck{mtx_};
            if (stopped_) {
                co_return std::nullopt;
            }
        }

        co_await cv_.wait();
        co_return pop_front();
    }
};
}  // namespace detail

template <typename T>
using Sender = std::shared_ptr<std::function<void(const T&)>>;

template <typename T>
using Receiver = std::unique_ptr<std::function<asio::awaitable<std::optional<T>>()>>;

template <typename T, bool threadsafe = true>
auto make_mpsc() -> std::tuple<Sender<T>, Receiver<T>> {
    using Sender   = Sender<T>;
    using Receiver = Receiver<T>;
#define MAKE_MPSC_IMPL(ctx)                                                               \
    do {                                                                                  \
        auto defer = std::make_shared<gsl::final_action<std::function<void()>>>(          \
            [ctx] { ctx->close(); });                                                     \
        Sender sender0 = std::make_shared<typename Sender::element_type>(                 \
            [ctx, defer](auto&& a) { ctx->send(std::forward<decltype(a)>(a)); });         \
        Receiver receiver0 = std::make_unique<typename Receiver::element_type>(           \
            [ctx]() -> asio::task<std::optional<T>> { co_return co_await ctx->recv(); }); \
        return std::make_tuple(sender0, std::move(receiver0));                            \
    } while (0)

    if constexpr (threadsafe) {
        using ctx_t = detail::mpsc_context_t<T, std::mutex>;
        auto ctx    = std::make_shared<ctx_t>();
        MAKE_MPSC_IMPL(ctx);
    } else {
        using ctx_t = detail::mpsc_context_t<T>;
        auto ctx    = std::make_shared<ctx_t>();
        MAKE_MPSC_IMPL(ctx);
    }
}

}  // namespace chan
}  // namespace cc
