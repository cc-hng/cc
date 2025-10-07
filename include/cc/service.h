#pragma once

#include <any>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <typeinfo>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/callable_traits.hpp>
#include <cc/detail/functor.h>
#include <cc/type_traits.h>
#include <cc/util.h>

namespace cc {

namespace ct = boost::callable_traits;

// clang-format off
template <
    typename MutexPolicy = cc::NonMutex,
    template <class> class ReaderLock = cc::LockGuard,
    template <class> class WriterLock = cc::LockGuard>  // clang-format on
class Service final : boost::noncopyable {
    mutable MutexPolicy mtx_;
    std::unordered_map<std::string, detail::Functor> functors_;

public:
    static Service& instance() {
        static Service ins;
        return ins;
    }

public:
    Service()  = default;
    ~Service() = default;

    template <typename Fn>
    void advertise(std::string_view svc, Fn&& fn) {
        WriterLock<MutexPolicy> _lck{mtx_};
        functors_.emplace(std::string(svc), detail::Functor(std::forward<Fn>(fn)));
    }

    template <typename MemFn, typename Cls>
    std::enable_if_t<std::is_member_function_pointer_v<MemFn>>
    advertise(std::string_view svc, const MemFn& mf, Cls obj) {
        WriterLock<MutexPolicy> _lck{mtx_};
        functors_.emplace(std::string(svc),
                          detail::Functor(detail::Functor::make_function(mf, obj)));
    }

    void unadvertise(const std::string& svc) {
        WriterLock<MutexPolicy> _lck{mtx_};
        functors_.erase(svc);
    }

    // clang-format off
    template <typename R, typename... Args>
    std::enable_if_t<cc::is_awaitable_v<R>, R>
    call(std::string_view svc, Args... args) const {
        auto lck    = std::make_unique<ReaderLock<MutexPolicy>>(mtx_);
        std::string svc0(svc);
        auto iter = functors_.find(svc0);
        if (iter == functors_.end()) {
            throw std::runtime_error("Service not found: " + svc0);
        }
        try {
            const auto& functor = iter->second;
            auto&& token = functor.template operator()<R>(std::forward<Args>(args)...);
            lck.reset();
            co_return co_await std::move(token);
        } catch (const std::exception& e) {
            throw std::runtime_error("Service error. svc=\"" + svc0 + "\", msg=\"" + e.what() + "\"");
        }
    }

    template <typename R, typename... Args>
    std::enable_if_t<!cc::is_awaitable_v<R>, R>
    call(std::string_view svc, Args&&... args) const {
        auto lck    = std::make_unique<ReaderLock<MutexPolicy>>(mtx_);
        std::string svc0(svc);
        auto iter = functors_.find(svc0);
        if (iter == functors_.end()) {
            throw std::runtime_error("Service not found: " + svc0);
        }
        try {
            const auto& functor = iter->second;
            return functor.template operator()<R>(std::forward<Args>(args)...);
        } catch (const std::exception& e) {
            throw std::runtime_error("Service error. svc=\"" + svc0 + "\", msg=\"" + e.what() + "\"");
        }
    }

    template <typename R, typename... Args>
    boost::asio::awaitable<R>  //
    co_call(std::string_view svc, Args... args) const {
        co_return co_await call<boost::asio::awaitable<R>, Args...>(svc, args...);
    }

    // clang-format off
};

using ConcurrentService = Service<std::shared_mutex, std::unique_lock, std::shared_lock>;

}  // namespace cc
