#pragma once

#include <any>
#include <functional>
#include <memory>
#include <string>
#include <boost/callable_traits.hpp>
#include <boost/core/demangle.hpp>
#include <cc/type_traits.h>
#include <cc/util.h>
#include <cc/value.h>

#ifdef CC_ENABLE_COROUTINE
#    include <cc/asio.hpp>
namespace net = boost::asio;
#endif

namespace cc {

namespace detail {

namespace ct = boost::callable_traits;

template <typename T>
struct adjust_tuple;

template <typename... Args>
struct adjust_tuple<std::tuple<Args...>> {
    using type = std::tuple<std::decay_t<Args>...>;
};

class Functor {
    bool is_coro_;
    std::string signature_;
    std::any fn_;
    std::any varfn_;

    template <typename Fn>
    static auto make_copyable_function(Fn&& f) {
        using DF = std::function<ct::function_type_t<std::decay_t<Fn>>>;
        auto spf = std::make_shared<std::decay_t<Fn>>(std::forward<Fn>(f));
        return DF([spf](auto&&... args) -> decltype(auto) {
            return (*spf)(std::forward<decltype(args)>(args)...);
        });
    }

public:
    template <typename F>
    Functor(F&& f)
      : is_coro_(cc::is_awaitable_v<ct::return_type_t<F>>)
      , signature_(type_name<ct::function_type_t<std::decay_t<F>>>()) {
        using DF = std::function<ct::function_type_t<std::decay_t<F>>>;
        auto spf = std::make_shared<std::decay_t<F>>(std::forward<F>(f));
        fn_      = DF([spf](auto&&... args) -> decltype(auto) {
            return (*spf)(std::forward<decltype(args)>(args)...);
        });
        varfn_   = make_varfn(spf);
    }

    Functor(Functor&& other)            = default;
    Functor& operator=(Functor&& other) = default;

    template <typename Memfn, typename Cls>
    static std::enable_if_t<std::is_member_function_pointer_v<Memfn>,
                            std::function<cc::remove_member_pointer_t<Memfn>>>
    make_function(Memfn fn, Cls obj) {
        return [fn, obj](auto&&... args) -> decltype(auto) {
            if constexpr (std::is_pointer_v<Cls>) {
                return (obj->*fn)(std::forward<decltype(args)>(args)...);
            } else {
                static_assert(cc::has_member_get_v<Cls>, "Cls expect smart pointer.");
                auto origin = obj.get();
                return (origin->*fn)(std::forward<decltype(args)>(args)...);
            }
        };
    }

#ifdef CC_ENABLE_COROUTINE
    template <typename R = void, typename... Args>
    std::enable_if_t<cc::is_awaitable_v<R>, R>  //
    operator()(Args&&... args) const {
        using Inner = typename R::value_type;
        if (is_coro_) {
            co_return co_await do_call<Inner>(Int2Type<1>{}, std::forward<Args>(args)...);
        } else {
            co_return do_call<Inner>(Int2Type<0>{}, std::forward<Args>(args)...);
        }
    }
#endif

    template <typename R = void, typename... Args>
    std::enable_if_t<!cc::is_awaitable_v<R>, R>  //
    operator()(Args&&... args) const {
        return do_call<R>(Int2Type<0>{}, std::forward<Args>(args)...);
    }

private:
    template <typename R, typename... Args>
    std::enable_if_t<!cc::is_awaitable_v<R>, R>  //
    do_call(Int2Type<0>, Args&&... args) const {
        using ArgsTuple = std::tuple<std::decay_t<Args>...>;
        try {
            if constexpr (std::is_same_v<ArgsTuple, std::tuple<var_t>>) {
                var_t arg0 = std::get<0>(ArgsTuple{std::forward<Args>(args)...});
                const std::function<R(const var_t&)>* fn = nullptr;

                try {
                    fn = std::any_cast<std::function<R(const var_t&)>>(&fn_);
                } catch (const std::bad_any_cast& e) {
                }

                if (fn) {
                    return (*fn)(arg0);
                } else {
                    const auto* varfn = std::any_cast<std::function<var_t(var_t)>>(&varfn_);
                    auto r            = (*varfn)(arg0);
                    if constexpr (!std::is_same_v<R, void>) {
                        return var::as<R>(r);
                    }
                }
            } else {
                using DF        = std::function<R(Args...)>;
                const auto* spf = std::any_cast<DF>(&fn_);
                return (*spf)(std::forward<Args>(args)...);
            }
        } catch (const std::bad_any_cast& e) {
            auto signature = type_name<R(Args...)>();
            std::throw_with_nested(std::runtime_error("Call with wrong arguments. expect='" + signature
                                                      + "', actual='" + signature + "'"));
        }
    }

#ifdef CC_ENABLE_COROUTINE
    template <typename Inner, typename... Args>
    std::enable_if_t<!cc::is_awaitable_v<Inner>, net::awaitable<Inner>>  //
    do_call(Int2Type<1>, Args&&... args) const {
        using ArgsTuple = std::tuple<std::decay_t<Args>...>;
        try {
            if constexpr (std::is_same_v<ArgsTuple, std::tuple<var_t>>) {
                var_t arg0 = std::get<0>(ArgsTuple{std::forward<Args>(args)...});
                const std::function<net::awaitable<Inner>(const var_t&)>* fn = nullptr;
                try {
                    const auto* fn =
                        std::any_cast<std::function<net::awaitable<Inner>(const var_t&)>>(&fn_);
                } catch (const std::bad_any_cast& e) {
                }

                if (!fn) {
                    const auto* varfn =
                        std::any_cast<std::function<net::awaitable<var_t>(var_t)>>(&varfn_);
                    auto r = co_await (*varfn)(arg0);
                    if constexpr (!std::is_same_v<Inner, void>) {
                        co_return var::as<Inner>(r);
                    }
                } else {
                    co_return co_await (*fn)(arg0);
                }
            } else {
                using DF        = std::function<net::awaitable<Inner>(Args...)>;
                const auto* spf = std::any_cast<DF>(&fn_);
                co_return co_await (*spf)(std::forward<Args>(args)...);
            }
        } catch (const std::bad_any_cast& e) {
            auto signature = type_name<net::awaitable<Inner>(Args...)>();
            std::throw_with_nested(std::runtime_error("Call with wrong arguments. expect='" + signature
                                                      + "', actual='" + signature + "'"));
        }
    }

    template <typename F>
    static std::enable_if_t<cc::is_awaitable_v<ct::return_type_t<F>>,
                            std::function<net::awaitable<var_t>(var_t)>>
    make_varfn(std::shared_ptr<F> spf) {
        using ArgsTuple = detail::adjust_tuple<ct::args_t<F>>::type;
        using R         = ct::return_type_t<F>;
        return [spf](var_t v) -> net::awaitable<var_t> {
            if constexpr (std::is_same_v<R, net::awaitable<void>>) {
                co_await std::apply(*spf, var::as<ArgsTuple>(v));
                co_return var_t();
            } else {
                co_return var_t(co_await std::apply(*spf, var::as<ArgsTuple>(v)));
            }
        };
    }
#endif

    template <typename F>
    static std::enable_if_t<!cc::is_awaitable_v<ct::return_type_t<F>>,  //
                            std::function<var_t(var_t)>>
    make_varfn(std::shared_ptr<F> spf) {
        using R         = ct::return_type_t<F>;
        using ArgsTuple = detail::adjust_tuple<ct::args_t<F>>::type;
        return [spf](var_t v) -> var_t {
            if constexpr (std::is_same_v<R, void>) {
                std::apply(*spf, var::as<ArgsTuple>(v));
                return var_t();
            } else {
                return var_t(std::apply(*spf, var::as<ArgsTuple>(v)));
            }
        };
    }
};

}  // namespace detail
}  // namespace cc
