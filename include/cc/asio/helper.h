#pragma once

#include <chrono>
#include <concepts>
#include <exception>
#include <type_traits>
#include <utility>
// pi-lens-ignore: header-not-found
#include <boost/asio/awaitable.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/use_awaitable.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/this_coro.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/post.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/bind_executor.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/steady_timer.hpp>
// pi-lens-ignore: header-not-found
#include <boost/callable_traits.hpp>

namespace cc {

namespace net = boost::asio;  // NOLINT

namespace ct = boost::callable_traits;

namespace detail {

template <typename T>
struct task_held_type;

template <typename T, typename Executor>
struct task_held_type<net::awaitable<T, Executor>> {
    using type = std::decay_t<T>;
};

template <typename T>
using task_inner_t = typename task_held_type<T>::type;

template <typename T>
struct is_task : std::false_type {};

template <typename... Args>
struct is_task<net::awaitable<Args...>> : std::true_type {};

// Result f() yields once awaited: a plain callable's return type, or a
// coroutine's inner type.
template <typename Raw, bool = is_task<Raw>::value>
struct schedule_result {
    using type = Raw;
};

template <typename Raw>
struct schedule_result<Raw, true> {
    using type = task_inner_t<Raw>;
};

template <typename Fn>
using schedule_result_t = typename schedule_result<ct::return_type_t<Fn>>::type;

// Uniform awaitable entry point: wraps a plain callable, passes a coroutine's
// awaitable through, so schedule() needs only one body.
template <typename Fn, typename... Args>
    requires(!is_task<ct::return_type_t<Fn>>::value)
net::awaitable<ct::return_type_t<Fn>> as_task(Fn&& f, Args&&... args) {
    if constexpr (std::is_void_v<ct::return_type_t<Fn>>) {
        std::forward<Fn>(f)(std::forward<Args>(args)...);
        co_return;
    } else {
        co_return std::forward<Fn>(f)(std::forward<Args>(args)...);
    }
}

template <typename Fn, typename... Args>
    requires is_task<ct::return_type_t<Fn>>::value
ct::return_type_t<Fn> as_task(Fn&& f, Args&&... args) {
    return std::forward<Fn>(f)(std::forward<Args>(args)...);
}

}  // namespace detail

inline net::awaitable<void>  //
timeout(int ms) {
    if (ms <= 0) {
        co_await net::post(co_await net::this_coro::executor, net::use_awaitable);
    } else {
        net::steady_timer timer(co_await net::this_coro::executor);
        timer.expires_after(std::chrono::milliseconds(ms));
        co_await timer.async_wait(net::use_awaitable);
    }
}

/// Schedule f(args...) on the io_context; returns net::awaitable<result>.
/// Accepts both plain callables and coroutines (returning net::awaitable).
/// Fn and Args are held by reference across suspension; keep them alive until the awaitable completes.
template <typename Context, typename Fn, typename... Args>
    requires requires(Context& ctx) { ctx.get_executor(); }
net::awaitable<detail::schedule_result_t<Fn>>  //
schedule(Context& ctx, Fn&& f, Args&&... args) {
    std::exception_ptr e;
    auto current_ctx = co_await net::this_coro::executor;
    co_await net::post(net::bind_executor(ctx.get_executor(), net::use_awaitable));
    try {
        using R = detail::schedule_result_t<Fn>;
        if constexpr (std::is_void_v<R>) {
            co_await detail::as_task(std::forward<Fn>(f), std::forward<Args>(args)...);
            co_await net::post(net::bind_executor(current_ctx, net::use_awaitable));
            co_return;
        } else {
            auto r = co_await detail::as_task(std::forward<Fn>(f), std::forward<Args>(args)...);
            co_await net::post(net::bind_executor(current_ctx, net::use_awaitable));
            co_return r;
        }
    } catch (...) {
        e = std::current_exception();
    }
    co_await net::post(net::bind_executor(current_ctx, net::use_awaitable));
    if (e) [[unlikely]] {
        std::rethrow_exception(e);
    }
}

}  // namespace cc
