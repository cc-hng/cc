#pragma once

#include <any>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <boost/callable_traits.hpp>
#include <boost/core/noncopyable.hpp>
#include <cc/util.h>

namespace cc {

namespace ct = boost::callable_traits;

namespace detail {

template <typename Signature>
struct adjust_signature;

template <typename R, typename... Args>
struct adjust_signature<R(Args...)> {
    using type = R(const std::decay_t<Args>&...);
};

}  // namespace detail

template <class MutexPolicy = NonMutex, template <class> class ReaderLock = LockGuard,
          template <class> class WriterLock = LockGuard>
class Signal : boost::noncopyable {
    using handler_t = int;

public:
    Signal() = default;

    template <typename Fn>
    std::enable_if_t<!std::is_member_function_pointer_v<Fn>, handler_t>
    sub(std::string_view topic, Fn&& f) {
        using Signature = typename detail::adjust_signature<ct::function_type_t<Fn>>::type;
        WriterLock<MutexPolicy> _lck{mtx_};
        std::function<Signature> ff = std::forward<Fn>(f);
        return sub_impl(topic, std::move(ff));
    }

    template <typename MemFn, typename Cls>
    std::enable_if_t<std::is_member_function_pointer_v<MemFn>, handler_t>
    sub(std::string_view topic, const MemFn& fn, Cls obj) {
        using Signature =
            typename detail::adjust_signature<remove_member_pointer_t<MemFn>>::type;
        auto ff = std::function<Signature>([fn, obj](auto&&... args) {
            if constexpr (std::is_pointer_v<Cls>) {
                return (obj->*fn)(std::forward<decltype(args)>(args)...);
            } else {
                static constexpr auto has_get =
                    boost::hana::is_valid([](auto&& obj) -> decltype(obj.get()) {});
                BOOST_HANA_CONSTANT_CHECK(has_get(obj));
                auto origin = obj.get();
                return (origin->*fn)(std::forward<decltype(args)>(args)...);
            }
        });

        WriterLock<MutexPolicy> _lck{mtx_};
        return sub_impl(topic, std::move(ff));
    }

    template <typename... Args>
    void pub(std::string_view topic, Args&&... args) {
        using Signature = typename detail::adjust_signature<void(Args...)>::type;
        ReaderLock<MutexPolicy> _lck{mtx_};
        auto range = registry_.equal_range(std::string(topic));
        for (auto it = range.first; it != range.second; ++it) {
            const auto* f =
                std::any_cast<std::function<Signature>>(&(handlers_.at(it->second)));
            (*f)(args...);
        }
    }

    void unsub(std::string_view topic) {
        WriterLock<MutexPolicy> _lck{mtx_};
        auto range = registry_.equal_range(std::string(topic));
        for (auto it = range.first; it != range.second; ++it) {
            handlers_.erase(it->second);
        }
        registry_.erase(std::string(topic));
    }

    void unsub(handler_t id) {
        WriterLock<MutexPolicy> _lck{mtx_};
        handlers_.erase(id);

        auto it = registry_.begin();
        while (it != registry_.end()) {
            if (it->second == id) {
                it = registry_.erase(it);
                break;
            } else {
                ++it;
            }
        }
    }

private:
    template <typename Signature>
    handler_t sub_impl(std::string_view topic, std::function<Signature>&& f) {
        handler_t h = next_id_++;
        registry_.emplace(std::string(topic), h);
        handlers_.emplace(h, std::move(f));
        return h;
    }

private:
    MutexPolicy mtx_;
    handler_t next_id_ = 0;
    std::unordered_multimap<std::string, handler_t> registry_;
    std::unordered_map<handler_t, std::any> handlers_;
};

using ConcurrentSignal = Signal<std::shared_mutex, std::shared_lock>;

}  // namespace cc
