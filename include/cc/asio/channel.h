#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <tuple>
#include <cc/asio/condvar.h>

namespace cc {
namespace chan {

namespace detail {
template <typename T, typename MutexPolicy = NonMutex,
          template <class> class Lock = LockGuard>
struct mpsc_context_t {
    MutexPolicy mtx_;
    CondVar<MutexPolicy> cv_;
    std::deque<T> queue_;

    template <typename A>
    void send(A&& a) noexcept {
        do {
            Lock<MutexPolicy> _lck{mtx_};
            queue_.emplace_back(std::forward<A>(a));
        } while (0);
        cv_.notify_all();
    }

    asio::awaitable<std::deque<T>> recv() {
        std::deque<T> ret;
        do {
            Lock<MutexPolicy> _lck{mtx_};
            ret = std::move(queue_);
            co_return ret;
        } while (0);

        co_await cv_.wait();
        Lock<MutexPolicy> _lck{mtx_};
        ret = std::move(queue_);
        co_return ret;
    }
};
}  // namespace detail

template <typename T, bool threadsafe = true>
auto make_mpsc() {
#define MAKE_MPSC_IMPL(ctx)                                                               \
    do {                                                                                  \
        using Sender                   = std::function<void(const T&)>;                   \
        using Receiver                 = std::function<asio::awaitable<std::deque<T>>()>; \
        std::shared_ptr<Sender> sender = std::make_shared<Sender>(                        \
            [ctx](auto&& a) { ctx->send(std::forward<decltype(a)>(a)); });                \
        std::unique_ptr<Receiver> receiver = std::make_unique<Receiver>(                  \
            [ctx]() -> asio::task<std::deque<T>> { co_return co_await ctx->recv(); });    \
        return std::make_tuple(sender, std::move(receiver));                              \
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
