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
#include <cc/stopwatch.h>
#include <cc/type_traits.h>
#include <cc/util.h>

#ifdef CC_ENABLE_COROUTINE
#    include <cc/asio.hpp>
#endif

namespace cc {

namespace ct = boost::callable_traits;

namespace detail {

template <typename Signature>
struct adjust_signature;

template <typename R, typename... Args>
struct adjust_signature<R(Args...)> {
    using type = R(const std::decay_t<Args>&...);
};

template <typename Signature>
std::string type_name() {
    return boost::core::demangle(typeid(Signature).name());
}

// clang-format off
template <
    typename MutexPolicy = NonMutex,
    template <class> class ReaderLock = LockGuard,
    template <class> class WriterLock = LockGuard
>  // clang-format on
class TimeTracker : boost::noncopyable {
    MutexPolicy mtx_;
    const double duration_;
    const int times_;
    std::deque<cc::StopWatch> data_;

public:
    TimeTracker(double duration, int times) : duration_(duration), times_(times) {}

    ~TimeTracker() = default;

    bool track() noexcept {
        WriterLock<MutexPolicy> _lck{mtx_};
        if (data_.size() < times_) {
            data_.emplace_back(cc::StopWatch());
        } else {
            if (data_.front().elapsed() < duration_) {
                return false;
            } else {
                data_.pop_front();
                data_.emplace_back(cc::StopWatch());
            }
        }
        return true;
    }
};

}  // namespace detail

// clang-format off
template <
    typename MutexPolicy = NonMutex,
    template <class> class ReaderLock = LockGuard,
    template <class> class WriterLock = LockGuard
>  // clang-format on
class Signal : boost::noncopyable {
    using handler_t = int;

public:
    Signal() = default;

    /// 订阅某个话题
    ///
    /// @param topic    话题
    /// @param f        回调
    /// @return handler_t  句柄id
    template <typename Fn>
    std::enable_if_t<!std::is_member_function_pointer_v<Fn>, handler_t>
    sub(std::string_view topic, Fn&& f) {
        WriterLock<MutexPolicy> _lck{mtx_};
        return sub_impl(topic, make_sig_handle(std::forward<Fn>(f)));
    }

    /// 限制某个话题一段时间内(duration)最多订阅次数(times)
    ///
    /// @param topic    话题
    /// @param duration 时间段，单位秒
    /// @param times    次数
    /// @param f        回调
    /// @return handler_t  句柄id
    template <typename Fn, typename = std::enable_if_t<!std::is_member_function_pointer_v<Fn>>>
    handler_t sub(std::string_view topic, double duration, int times, Fn&& f) {
        using T         = detail::TimeTracker<MutexPolicy, ReaderLock, WriterLock>;
        auto tracker    = std::make_shared<T>(duration, times);
        auto f0         = make_sig_handle(std::forward<Fn>(f));
        decltype(f0) nf = [tracker, duration, times, f0 = std::move(f0)](const auto&... args) {
            if (tracker->track()) {
                f0(args...);
            }
        };
        return sub(topic, std::move(nf));
    }

    /// 限制某个话题每隔一段时间内(duration)最多订阅一次
    ///
    /// @param topic    话题
    /// @param duration 时间段，单位秒
    /// @param f        回调
    /// @return handler_t  句柄id
    template <typename Fn, typename = std::enable_if_t<!std::is_member_function_pointer_v<Fn>>>
    handler_t sub(std::string_view topic, double duration, Fn&& f) {
        return sub(topic, duration, 1, std::forward<Fn>(f));
    }

    template <typename MemFn, typename Cls>
    std::enable_if_t<std::is_member_function_pointer_v<MemFn>, handler_t>
    sub(std::string_view topic, const MemFn& fn, Cls obj) {
        return sub(topic, make_sig_handle(fn, obj));
    }

    template <typename MemFn, typename Cls>
    std::enable_if_t<std::is_member_function_pointer_v<MemFn>, handler_t>
    sub(std::string_view topic, double duration, int times, const MemFn& fn, Cls obj) {
        return sub(topic, duration, times, make_sig_handle(fn, obj));
    }

    template <typename MemFn, typename Cls>
    std::enable_if_t<std::is_member_function_pointer_v<MemFn>, handler_t>
    sub(std::string_view topic, double duration, const MemFn& fn, Cls obj) {
        return sub(topic, duration, 1, make_sig_handle(fn, obj));
    }

    template <typename... Args>
    void pub(std::string_view topic, Args&&... args) {
        using Signature = typename detail::adjust_signature<void(Args...)>::type;
        std::string topic0(topic);
        if (topic_types_.count(topic0)) {
            auto typname = detail::type_name<Signature>();
            if (topic_types_.at(topic0) != typname) {
                throw std::runtime_error("Signal::pub type dismatch. registed="
                                         + topic_types_.at(topic0) + ", current=" + typname);
            }
        }

        ReaderLock<MutexPolicy> _lck{mtx_};
        auto range = registry_.equal_range(topic0);
        for (auto it = range.first; it != range.second; ++it) {
            const auto* f = std::any_cast<std::function<Signature>>(&(handlers_.at(it->second)));
            (*f)(args...);
        }
    }

    void unsub(std::string_view topic) {
        std::string topic0(topic);
        WriterLock<MutexPolicy> _lck{mtx_};
        auto range = registry_.equal_range(topic0);
        for (auto it = range.first; it != range.second; ++it) {
            handlers_.erase(it->second);
        }
        registry_.erase(topic0);
        topic_types_.erase(topic0);
    }

    void unsub(handler_t id) {
        WriterLock<MutexPolicy> _lck{mtx_};
        handlers_.erase(id);

        std::string topic;
        auto it = registry_.begin();
        while (it != registry_.end()) {
            if (it->second == id) {
                topic = it->first;
                it    = registry_.erase(it);
                break;
            } else {
                ++it;
            }
        }

        if (!registry_.contains(topic)) {
            topic_types_.erase(topic);
        }
    }

#ifdef CC_ENABLE_COROUTINE

    template <typename... Ts>
    std::tuple<handler_t, cc::chan::Receiver<std::tuple<Ts...>>> stream(std::string_view topic) {
        using R                 = std::tuple<Ts...>;
        auto [sender, receiver] = cc::chan::make_mpsc<R>();
        auto id =
            sub(topic, [sender0 = sender](Ts... args) { (*sender0)(std::make_tuple(args...)); });
        return std::make_tuple(id, std::move(receiver));
    }

#endif

private:
    template <typename Signature>
    handler_t sub_impl(std::string_view topic, std::function<Signature>&& f) {
        std::string topic0(topic);
        auto typname = detail::type_name<Signature>();
        if (topic_types_.contains(topic0)) {
            if (typname != topic_types_.at(topic0)) {
                throw std::runtime_error("Signal::sub type dismatch. before="
                                         + topic_types_.at(topic0) + ", after=" + typname);
            }
        } else {
            topic_types_.emplace(topic0, (std::string&&)typname);
        }
        handler_t h = next_id_++;
        registry_.emplace(std::string(topic), h);
        handlers_.emplace(h, std::move(f));
        return h;
    }

    template <typename Fn, typename = std::enable_if_t<!std::is_member_function_pointer_v<Fn>>>
    auto make_sig_handle(Fn&& f) {
        using Signature = typename detail::adjust_signature<ct::function_type_t<Fn>>::type;
        return std::function<Signature>(std::forward<Fn>(f));
    }

    template <typename MemFn, typename Cls,
              typename = std::enable_if_t<std::is_member_function_pointer_v<MemFn>>>
    auto make_sig_handle(const MemFn& fn, Cls obj) {
        using Signature = typename detail::adjust_signature<remove_member_pointer_t<MemFn>>::type;
        return std::function<Signature>([fn, obj](auto&&... args) {
            if constexpr (std::is_pointer_v<Cls>) {
                return (obj->*fn)(std::forward<decltype(args)>(args)...);
            } else {
                static_assert(cc::has_member_get_v<Cls>, "Cls expect smart pointer.");
                auto origin = obj.get();
                return (origin->*fn)(std::forward<decltype(args)>(args)...);
            }
        });
    }

private:
    MutexPolicy mtx_;
    handler_t next_id_ = 0;
    std::unordered_multimap<std::string, handler_t> registry_;
    std::unordered_map<handler_t, std::any> handlers_;
    std::unordered_map<std::string, std::string> topic_types_;
};

using ConcurrentSignal = Signal<std::recursive_mutex>;

}  // namespace cc
