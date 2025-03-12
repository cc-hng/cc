#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <tuple>
#include <cc/asio/condvar.h>
#include <gsl/gsl>

namespace cc {
namespace chan {

namespace detail {

// clang-format off
template <
    typename T,
    typename MutexPolicy = NonMutex,
    template <class> class Lock = LockGuard
>  // clang-format on
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

    asio::awaitable<std::deque<T>> recv() {
        std::deque<T> ret;
        do {
            Lock<MutexPolicy> _lck{mtx_};
            if (!stopped_) {
                if (queue_.size()) {
                    ret = std::move(queue_);
                    co_return ret;
                }
            } else {
                co_return ret;
            }
        } while (0);

        co_await cv_.wait();
        Lock<MutexPolicy> _lck{mtx_};
        ret = std::move(queue_);
        co_return ret;
    }
};

template <typename T,                                   //
          typename MutexPolicy        = cc::NonMutex,   //
          template <class> class Lock = cc::LockGuard>  //
class OneshotContext {
    MutexPolicy mtx_;
    cc::CondVar<MutexPolicy> cv_;
    bool sent_     = false;
    bool received_ = false;
    T val_;

public:
    template <typename A>
    void send(A&& a) {
        do {
            Lock<MutexPolicy> _lck{mtx_};
            if (sent_) {
                throw std::runtime_error("Oneshot channel can only send once");
            }
            sent_ = true;
            val_  = std::forward<A>(a);
        } while (0);
        cv_.notify_all();
    }

    asio::task<T> recv() {
        do {
            Lock<MutexPolicy> _lck{mtx_};
            if (received_) {
                throw std::runtime_error("Oneshot channel can only receive one value!");
            }
            received_ = true;

            if (sent_) {
                co_return val_;
            }
        } while (0);

        co_await cv_.wait();
        Lock<MutexPolicy> _lck{mtx_};
        co_return val_;
    }
};

}  // namespace detail

namespace mpsc {

template <typename T>
using Sender = std::shared_ptr<std::function<void(const T&)>>;

template <typename T>
using Receiver = std::unique_ptr<std::function<asio::awaitable<std::deque<T>>()>>;

template <typename T, bool threadsafe = true>
auto make() -> std::tuple<Sender<T>, Receiver<T>> {
    using Sender   = Sender<T>;
    using Receiver = Receiver<T>;
#define MAKE_MPSC_IMPL(ctx)                                                                      \
    do {                                                                                         \
        auto defer =                                                                             \
            std::make_shared<gsl::final_action<std::function<void()>>>([ctx] { ctx->close(); }); \
        Sender sender0 = std::make_shared<typename Sender::element_type>(                        \
            [ctx, defer](auto&& a) { ctx->send(std::forward<decltype(a)>(a)); });                \
        Receiver receiver0 = std::make_unique<typename Receiver::element_type>(                  \
            [ctx]() -> asio::task<std::deque<T>> { co_return co_await ctx->recv(); });           \
        return std::make_tuple(sender0, std::move(receiver0));                                   \
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

}  // namespace mpsc

namespace oneshot {

template <typename T, bool threadsafe = true>
auto make() {
    if constexpr (threadsafe) {
        return std::make_unique<detail::OneshotContext<T, std::mutex>>();
    } else {
        return std::make_unique<detail::OneshotContext<T>>();
    }
}

}  // namespace oneshot

}  // namespace chan
}  // namespace cc
