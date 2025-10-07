#pragma once

#include <any>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <boost/callable_traits.hpp>
#include <boost/core/demangle.hpp>
#include <boost/core/noncopyable.hpp>
#include <cc/detail/functor.h>
#include <cc/stopwatch.h>
#include <cc/type_traits.h>
#include <cc/util.h>

#ifdef CC_ENABLE_COROUTINE
#    include <cc/asio.hpp>
#endif

namespace cc {

namespace ct = boost::callable_traits;

namespace detail {

template <typename T>
struct tuple_name;

template <typename... Args>
struct tuple_name<std::tuple<Args...>> {
    static inline std::string str() { return type_name<Args...>(); }
};

template <typename F>
struct sig_adjust_signature;

template <typename R, typename... Args>
struct sig_adjust_signature<R(Args...)> {
    using type = R(const std::decay_t<Args>&...);
};

}  // namespace detail

template <                                          //
    typename MutexPolicy              = NonMutex,   //
    template <class> class ReaderLock = LockGuard,  //
    template <class> class WriterLock = LockGuard>
class Signal : boost::noncopyable {
    using Handle = int;

    MutexPolicy mtx_;
    Handle id_;
    std::unordered_multimap<std::string, Handle> registries_;
    std::unordered_map<Handle, cc::detail::Functor> handlers_;
    std::unordered_map<std::string, std::string> topic_info_;

    template <typename F>
    static auto make_cv_function(F&& f) {
        using DF =
            std::function<typename detail::sig_adjust_signature<ct::function_type_t<F>>::type>;
        if constexpr (std::tuple_size_v<ct::args_t<F>> > 0) {
            auto spf = std::make_shared<std::decay_t<F>>(std::forward<F>(f));
            return DF([spf](const auto&... args) { return (*spf)(args...); });
        } else {
            return std::forward<F>(f);
        }
    }

public:
    Signal() = default;

    std::unordered_map<std::string, std::string>  //
    list() {
        ReaderLock<MutexPolicy> _lck{mtx_};
        std::unordered_map<std::string, std::string> ret = topic_info_;
        return ret;
    }

    template <typename F>
    std::enable_if_t<!std::is_member_function_pointer_v<F>, Handle>  //
    sub(std::string topic, F&& f) {
        using ArgsTuple = detail::adjust_tuple<ct::args_t<F>>::type;
        auto typeinfo   = detail::tuple_name<ArgsTuple>::str();
        WriterLock<MutexPolicy> _lck{mtx_};
        return sub_impl(std::move(topic), std::move(typeinfo),
                        detail::Functor(make_cv_function(std::forward<F>(f))));
    }

    template <typename MemFn, typename Cls>
    std::enable_if_t<std::is_member_function_pointer_v<MemFn>, Handle>
    sub(std::string topic, const MemFn& fn, Cls obj) {
        using ArgsTuple = detail::adjust_tuple<ct::args_t<cc::remove_member_pointer_t<MemFn>>>::type;
        auto typeinfo = detail::tuple_name<ArgsTuple>::str();
        WriterLock<MutexPolicy> _lck{mtx_};
        return sub_impl(std::move(topic), std::move(typeinfo),
                        detail::Functor(make_cv_function(detail::Functor::make_function(fn, obj))));
    }

    template <typename... Args>
    void pub(std::string topic, Args&&... args) {
        using ArgsTuple = detail::adjust_tuple<std::tuple<std::remove_cv_t<Args>...>>::type;

        ReaderLock<MutexPolicy> _lck{mtx_};
        if (topic_info_.count(topic)) {
            auto name = detail::tuple_name<ArgsTuple>::str();
            if (topic_info_.at(topic) != name && !std::is_same_v<ArgsTuple, std::tuple<var_t>>) {
                throw std::runtime_error("Signal::pub type dismatch. registed="
                                         + topic_info_.at(topic) + ", current=" + name);
            }

            auto range = registries_.equal_range(topic);
            if (range.first != range.second) {
                std::tuple<std::decay_t<Args>...> args0(std::forward<Args>(args)...);
                for (auto it = range.first; it != range.second; ++it) {
                    auto* f = &(handlers_.at(it->second));
                    std::apply([f](const auto&... as) { (*f)(as...); }, args0);
                }
            }
        }
    }

    void unsub(const std::string& topic) {
        WriterLock<MutexPolicy> _lck{mtx_};
        auto range = registries_.equal_range(topic);
        for (auto it = range.first; it != range.second; ++it) {
            handlers_.erase(it->second);
        }
        registries_.erase(topic);
        topic_info_.erase(topic);
    }

    void unsub(Handle id) {
        WriterLock<MutexPolicy> _lck{mtx_};
        handlers_.erase(id);
        std::string topic;
        auto it = registries_.begin();
        while (it != registries_.end()) {
            if (it->second == id) {
                topic = it->first;
                it    = registries_.erase(it);
                break;
            } else {
                ++it;
            }
        }

        if (!registries_.contains(topic)) {
            topic_info_.erase(topic);
        }
    }

private:
    Handle sub_impl(std::string topic, std::string typeinfo, cc::detail::Functor&& f) {
        if (topic_info_.count(topic)) {
            if (typeinfo != topic_info_.at(topic)) {
                throw std::runtime_error("Signal::sub type dismatch. before="
                                         + topic_info_.at(topic) + ", after=" + typeinfo);
            }
        } else {
            topic_info_.emplace(topic, (std::string&&)typeinfo);
        }

        Handle h = id_++;
        registries_.emplace(topic, h);
        handlers_.emplace(h, (cc::detail::Functor&&)f);
        return h;
    }
};

using ConcurrentSignal = Signal<std::recursive_mutex>;

}  // namespace cc
