#pragma once

#include <any>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <typeinfo>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/callable_traits.hpp>
#include <cc/type_traits.h>
#include <cc/util.h>

namespace cc {

namespace ct = boost::callable_traits;

namespace detail {

/**
 * @class Functor
 * @brief bind fn/member_fn to functor
 *
 */
class Functor {
    std::string signature_;
    std::string return_type_;
    std::any function_;

    template <typename Fn>
    static auto make_copyable_function(Fn&& f) {
        using DF = std::decay_t<Fn>;
        auto spf = std::make_shared<DF>(std::forward<Fn>(f));
        return [spf](auto&&... args) -> decltype(auto) {
            return (*spf)(std::forward<decltype(args)>(args)...);
        };
    }

public:
    // clang-format off
    template <
        typename F,
        typename = std::enable_if_t<!std::is_member_function_pointer_v<F> && !std::is_same_v<F, Functor>>>  // clang-format on
    Functor(F&& f)
      : signature_(type_name<ct::function_type_t<F>>())
      , return_type_(type_name<ct::return_type_t<F>>())
      , function_(std::function<ct::function_type_t<F>>{std::forward<F>(f)}) {}

    // clang-format off
    template <
        typename MemFn,
        typename Cls,
        typename           = std::enable_if_t<std::is_member_function_pointer_v<MemFn>>,
        typename Signature = cc::remove_member_pointer_t<MemFn>>  // clang-format on
    Functor(const MemFn& mf, Cls obj)
      : Functor(std::function<Signature>{[mf, obj](auto&&... args) {
          if constexpr (std::is_pointer_v<Cls>) {
              return (obj->*mf)(std::forward<decltype(args)>(args)...);
          } else {
              static_assert(cc::has_member_get_v<Cls>, "Cls expect smart pointer.");
              auto origin = obj.get();
              return (origin->*mf)(std::forward<decltype(args)>(args)...);
          }
      }}) {}

    ~Functor() noexcept = default;

    Functor(Functor&&)            = default;
    Functor& operator=(Functor&&) = default;

    template <typename R>
    bool check_return_type() const {
        return return_type_ == type_name<R>();
    }

    template <typename R, typename... Args>
    R operator()(Args... args) const {
        if (signature_ != type_name<R(Args...)>()) {
            throw std::runtime_error("Signature dismatch!!! register=" + signature_
                                     + ", current=" + type_name<R(Args...)>());
        }

        const auto* f = std::any_cast<std::function<R(Args...)>>(&function_);
        return (*f)(args...);
    }
};

}  // namespace detail

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
        detail::Functor f(std::forward<Fn>(fn));
        functors_.emplace(std::string(svc), std::move(f));
    }

    template <typename MemFn, typename Cls>
    std::enable_if_t<std::is_member_function_pointer_v<MemFn>>
    advertise(std::string_view svc, const MemFn& mf, Cls obj) {
        WriterLock<MutexPolicy> _lck{mtx_};
        functors_.emplace(std::string(svc), detail::Functor{mf, obj});
    }

    void unadvertise(const std::string& svc) {
        WriterLock<MutexPolicy> _lck{mtx_};
        functors_.erase(svc);
    }

    // clang-format off
    template <typename R, typename... Args>
    std::enable_if_t<cc::is_awaitable_v<R>, R>  // clang-format on
    call(std::string_view svc, Args... args) const {
        using Inner = typename R::value_type;
        auto lck    = std::make_unique<ReaderLock<MutexPolicy>>(mtx_);
        std::string svc0(svc);
        auto iter = functors_.find(svc0);
        if (iter == functors_.end()) {
            throw std::runtime_error("Service not found: " + svc0);
        }
        try {
            const auto& functor = iter->second;
            if (functor.template check_return_type<R>()) {
                auto&& token = functor.template operator()<R, Args...>(args...);
                lck.reset();
                co_return co_await std::move(token);
            } else {
                co_return functor.template operator()<Inner, Args...>(args...);
            }
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
    template <typename R, typename... Args>
    std::enable_if_t<!cc::is_awaitable_v<R>, R>  // clang-format on
    call(std::string_view svc, Args... args) const {
        ReaderLock<MutexPolicy> _lck{mtx_};
        std::string svc0(svc);
        auto iter = functors_.find(svc0);
        if (iter == functors_.end()) {
            throw std::runtime_error("Service not found: " + svc0);
        }
        try {
            return (iter->second).template operator()<R, Args...>(args...);
        } catch (const std::exception& e) {
            throw std::runtime_error("Service error. svc=\"" + svc0 + "\", msg=\"" + e.what() + "\"");
        }
    }
};

using ConcurrentService = Service<std::shared_mutex, std::unique_lock, std::shared_lock>;

}  // namespace cc
