#pragma once

#include <mutex>
#include <unordered_map>
#include <boost/callable_traits.hpp>
#include <boost/signals2.hpp>
#include <cc/type_traits.h>
#include <cc/util.h>
#include <cc/value.h>
#include <gsl/gsl>

namespace cc {

namespace bs2 = boost::signals2;
namespace ct  = boost::callable_traits;

namespace detail {

template <typename T>
struct tuple_name;

template <typename... Args>
struct tuple_name<std::tuple<Args...>> {
    static inline std::string str() { return cc::type_name<Args...>(); }
};

template <typename U, typename T = std::remove_cvref_t<U>>
struct arg_convert {
    using type = std::conditional_t<std::is_class_v<T> || std::is_union_v<T>, const T&, T>;
};

template <typename F>
struct signature_convert;

template <typename R, typename... Args>
struct signature_convert<R(Args...)> {
    using type = R(typename arg_convert<Args>::type...);
};

}  // namespace detail

template <                                          //
    typename MutexPolicy              = NonMutex,   //
    template <class> class ReaderLock = LockGuard,  //
    template <class> class WriterLock = LockGuard>
class Signal {
    using sig_ptr    = std::shared_ptr<bs2::signal_base>;
    using mutex_type = bs2::keywords::mutex_type<
        std::conditional_t<std::is_same_v<MutexPolicy, NonMutex>, bs2::dummy_mutex, bs2::mutex>>;
    template <typename T>
    using sig_type = typename bs2::signal_type<T, mutex_type>::type;

    struct context_t {
        sig_type<void(const var_t&)> s_any;          // sub any
        std::function<void(const var_t&)> emit_any;  // pub any
        sig_ptr sig;
        std::string sign;
    };

    mutable MutexPolicy mtx_;
    std::unordered_map<std::string, context_t> sigs_;

public:
    template <typename MemFn, typename Cls>
    std::enable_if_t<std::is_member_function_pointer_v<MemFn>, bs2::connection>
    connect(std::string topic, const MemFn& fn, Cls obj) {
        static_assert(std::is_same_v<ct::return_type_t<MemFn>, void>,
                      "Only support void return type");
        std::function<remove_member_pointer_t<MemFn>> f = [obj, fn](auto&&... args) {
            if constexpr (std::is_pointer_v<Cls>) {
                (obj->*fn)(std::forward<decltype(args)>(args)...);
            } else if constexpr (is_shared_ptr_v<Cls>) {
                (obj.get()->*fn)(std::forward<decltype(args)>(args)...);
            } else if constexpr (is_weak_ptr_v<Cls>) {
                if (auto p = obj.lock()) {
                    (p.get()->*fn)(std::forward<decltype(args)>(args)...);
                }
            }
            // do nothing
        };
        return connect(std::move(topic), std::move(f));
    }

    template <typename F>
    std::enable_if_t<!std::is_member_function_pointer_v<F>, bs2::connection>
    connect(std::string topic, F&& f) {
        WriterLock<MutexPolicy> _lck{mtx_};
        using Signature      = typename detail::signature_convert<ct::function_type_t<F>>::type;
        const auto topicinfo = detail::tuple_name<ct::args_t<Signature>>::str();
        if (sigs_.find(topic) == sigs_.end()) {
            sigs_.emplace(topic, context_t{sig_type<void(const var_t&)>{}, nullptr, nullptr, ""});
        }

        auto& c = sigs_.at(topic);
        if (GSL_UNLIKELY(c.sig == nullptr && c.sign.empty())) {
            c.sig  = std::make_shared<sig_type<Signature>>();
            c.sign = topicinfo;
#pragma warning(push)
#pragma warning(disable : 4244)
            c.emit_any = [this, topic](const var_t& v) {
                using Tuple = typename adjust_tuple<ct::args_t<Signature>>::type;
                if (!v.is_array()) {
                    throw std::runtime_error("Signal type dismatch. not array");
                }

                if (std::tuple_size_v<Tuple> != v.as_array()->size()) {
                    throw std::runtime_error("Signal type dismatch. size not match");
                }
                std::apply([&](auto&&... xs) { emit(topic, std::forward<decltype(xs)>(xs)...); },
                           v.cast<Tuple>());
            };
#pragma warning(pop)
        }

        if (GSL_UNLIKELY(c.sign != topicinfo)) {
            throw std::runtime_error("Signal type dismatch. before=" + c.sign
                                     + ", after=" + topicinfo);
        }
        auto s = std::dynamic_pointer_cast<sig_type<Signature>>(c.sig);
        Expects(s);
        return s->connect(std::forward<F>(f));
    }

    void unconnect(std::string topic) {
        WriterLock<MutexPolicy> _lck{mtx_};
        sigs_.erase(topic);
    }

    template <typename... Args>
    void emit(std::string topic, Args&&... args) {
        using Signature      = typename detail::signature_convert<void(Args...)>::type;
        const auto topicinfo = detail::tuple_name<ct::args_t<Signature>>::str();

        ReaderLock<MutexPolicy> _lck{mtx_};
        if (sigs_.find(topic) != sigs_.end()) {
            auto& ctx       = sigs_.at(topic);
            auto args_tuple = std::forward_as_tuple(std::forward<Args>(args)...);

            if (ctx.sig) {
                if (ctx.sign != topicinfo) {
                    throw std::runtime_error("Signal type dismatch. registed=" + ctx.sign
                                             + ", current=" + topicinfo);
                }
                auto s = std::dynamic_pointer_cast<sig_type<Signature>>(ctx.sig);
                Expects(s);
                if (s && s->num_slots()) {
                    std::apply(*s, args_tuple);
                }
            }

            if (ctx.s_any.num_slots()) {
                var_t v = args_tuple;
                ctx.s_any(v);
            }
        }
    }

    /// debug
    template <typename F>
    auto connect_any(std::string topic, F&& f) {
        WriterLock<MutexPolicy> _lck{mtx_};
        if (sigs_.find(topic) == sigs_.end()) {
            sigs_.emplace(topic, context_t{sig_type<void(const var_t&)>{}, nullptr, nullptr, ""});
        }
        return sigs_.at(topic).s_any.connect(std::forward<F>(f));
    }

    void emit_any(std::string topic, const var_t& v) {
        ReaderLock<MutexPolicy> _lck{mtx_};
        if (sigs_.find(topic) != sigs_.end()) {
            sigs_.at(topic).emit_any(v);
        }
    }
};

using ConcurrentSignal = Signal<std::recursive_mutex>;

}  // namespace cc
