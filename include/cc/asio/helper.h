#pragma once

#include <boost/asio.hpp>
#include <cc/type_traits.h>
#include <gsl/gsl>

namespace asio = boost::asio;  // NOLINT

// alias
namespace boost {
namespace asio {
template <typename T>
using task = awaitable<T>;
}
}  // namespace boost

template <typename Fn, typename Signature = void()>
decltype(auto) co_wrap(Fn&& fn) {
    return [fn = std::forward<Fn>(fn)](BOOST_ASIO_COMPLETION_HANDLER_FOR(Signature) auto handler,
                                       auto&&... args1) {
        auto work = boost::asio::make_work_guard(handler);
        fn(std::forward<decltype(args1)>(args1)...,
           [handler = std::move(handler), work = std::move(work)](auto&&... args2) mutable {
               auto alloc =
                   boost::asio::get_associated_allocator(handler,
                                                         boost::asio::recycling_allocator<void>());

               boost::asio::dispatch(work.get_executor(),
                                     boost::asio::bind_allocator(
                                         alloc,
                                         [handler = std::move(handler),
                                          args3   = std::make_tuple(
                                              std::forward<decltype(args2)>(args2)...)]() mutable {
                                             std::apply(std::move(handler), std::move(args3));
                                         }));
           });
    };
}

namespace cc {

inline asio::task<void>  //
async_sleep(int ms) {
    if (ms <= 0) {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
    } else {
        asio::steady_timer timer(co_await asio::this_coro::executor);
        timer.expires_after(std::chrono::milliseconds(ms));
        co_await timer.async_wait(asio::use_awaitable);
    }
}

inline asio::task<void>  //
timeout(int ms) {
    co_return co_await async_sleep(ms);
}

/// 在ioc上调度程序f(args...)
/// 从当前executor上，切换到ioc执行f，然后再且回到当前executor
///
/// @pragma ioc asio executor
/// @pragma f 待执行函数
/// @pragma args... 函数参数
// clang-format off
template <typename Context, typename Fn, typename... Args>
asio::task<cc::result_of_t<Fn(Args...)>>  // clang-format on
async_schedule(Context& ioc, Fn&& f, Args&&... args) {
    using Ret = cc::result_of_t<Fn(Args...)>;
    std::exception_ptr e;
    auto cur_ctx = co_await asio::this_coro::executor;
    co_await asio::dispatch(asio::bind_executor(ioc.get_executor(), asio::use_awaitable));
    Ret r;
    try {
        r = std::forward<Fn>(f)(std::forward<Args>(args)...);
    } catch (...) {
        e = std::current_exception();
    }
    co_await asio::dispatch(asio::bind_executor(cur_ctx, asio::use_awaitable));
    if (GSL_UNLIKELY(e)) {
        std::rethrow_exception(e);
    }
    co_return r;
}

}  // namespace cc
