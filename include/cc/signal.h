#pragma once

#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <cc/functor.h>
#include <cc/util.h>

namespace cc {

template <class MutexPolicy = NonMutex, template <class> class ReaderLock = LockGuard,
          template <class> class WriterLock = LockGuard>
class Signal final {
public:
    using handler_t = int;

public:
    Signal() = default;

    template <typename Fn>
    std::enable_if_t<!std::is_member_function_pointer_v<Fn>, handler_t>
    register_handler(std::string_view topic, Fn&& f) {
        WriterLock<MutexPolicy> _lck{mtx_};
        return register_handler_impl(topic, cc::bind(std::forward<Fn>(f)));
    }

    template <typename MemFn, typename Cls>
    std::enable_if_t<std::is_member_function_pointer_v<MemFn>, handler_t>
    register_handler(std::string_view topic, const MemFn& f, Cls obj) {
        WriterLock<MutexPolicy> _lck{mtx_};
        return register_handler_impl(topic, cc::bind(f, obj));
    }

    template <typename... Args>
    void emit(std::string_view topic, Args&&... args) {
        ReaderLock<MutexPolicy> _lck{mtx_};
        auto range = registry_.equal_range(std::string(topic));
        for (auto it = range.first; it != range.second; ++it) {
            auto handler   = it->second;
            auto functorIt = handlers_.find(handler);
            if (functorIt != handlers_.end()) {
                cc::invoke(functorIt->second, args...);
            }
        }
    }

    void remove(std::string_view topic) {
        WriterLock<MutexPolicy> _lck{mtx_};
        auto range = registry_.equal_range(std::string(topic));
        for (auto it = range.first; it != range.second; ++it) {
            handlers_.erase(it->second);
        }
        registry_.erase(std::string(topic));
    }

    void remove(handler_t id) {
        WriterLock<MutexPolicy> _lck{mtx_};
        handlers_.erase(id);

        // 在注册表中查找并删除所有匹配的处理程序
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
    handler_t register_handler_impl(std::string_view topic, Functor<void>&& f) {
        handler_t h = next_id_++;
        registry_.emplace(std::string(topic), h);
        handlers_.emplace(h, std::move(f));
        return h;
    }

private:
    MutexPolicy mtx_;
    handler_t next_id_ = 0;
    std::unordered_multimap<std::string, handler_t> registry_;
    std::unordered_map<handler_t, Functor<void>> handlers_;
};

using ConcurrentSignal = Signal<std::shared_mutex, std::shared_lock>;

}  // namespace cc
