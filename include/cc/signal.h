#pragma once

#include <boost/callable_traits.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/signals2.hpp>
#include <cc/nameof.h>
#include <cc/util.h>
#include <gsl/gsl>

#if CC_USE_MUTEX
#include <mutex>
#endif

namespace cc {

namespace bs2 = boost::signals2;
namespace ct = boost::callable_traits;

class TypeMismatchError : public std::runtime_error {
public:
    TypeMismatchError(const std::string& expected, const std::string& actual)
        : std::runtime_error("type mismatch: expected signature " + expected + ", got " + actual) {}
};

namespace detail {
template <typename Func>
struct signature_convert;

template <typename Result, typename... Args>
struct signature_convert<Result(Args...)> {
    using type = Result(std::decay_t<Args>...);
};
}  // namespace detail

// Lock policy: ReadLock/WriteLock stay exclusive (LockGuard). Do NOT substitute
// a shared_mutex + shared_lock: ConcurrentSignal deliberately uses recursive_mutex
// so callbacks may reentrantly emit/connect. emit holding a read lock would
// deadlock when a callback acquires the same lock a second time on one thread.
template <MutexLike MutexPolicy = NonMutex, template <typename> typename ReadLock = LockGuard,
          template <typename> typename WriteLock = LockGuard>
class SignalImpl : boost::noncopyable {
    using sig_ptr = std::shared_ptr<bs2::signal_base>;
    using mutex_type = bs2::keywords::mutex_type<
        std::conditional_t<std::is_same_v<MutexPolicy, NonMutex>, bs2::dummy_mutex, bs2::mutex>>;
    template <typename T>
    using sig_type = typename bs2::signal_type<T, mutex_type>::type;

    struct context_t {
        sig_ptr sig;
        std::string typname;
    };

    mutable MutexPolicy mtx_;
    boost::container::flat_map<std::string, context_t, std::less<>> signals_;

public:
    template <typename Func>
        requires(!std::is_member_function_pointer_v<Func>)
    bs2::connection connect(std::string_view topic, Func&& f) {
        using Signature = typename detail::signature_convert<ct::function_type_t<Func>>::type;
        using ArgsTuple = ct::args_t<Signature>;
        using SigType = sig_type<Signature>;

        WriteLock<MutexPolicy> lck{mtx_};
        auto typname = CC_TYPENAME(ArgsTuple);

        // clang-format off
        auto it = signals_.find(topic);
        if (it == signals_.end()) {
            it = signals_.emplace(topic, context_t{std::make_shared<SigType>(), typname})
                        .first;
        }
        // clang-format on

        if (it->second.typname != typname) {
            throw TypeMismatchError(it->second.typname, typname);
        }

        auto s = std::static_pointer_cast<SigType>(it->second.sig);
        Expects(s);
        return s->connect(std::forward<Func>(f));
    }

    void disconnect(std::string_view topic) {
        WriteLock<MutexPolicy> lck{mtx_};
        signals_.erase(topic);
    }

    template <typename... Args>
    void emit(std::string_view topic, Args&&... args) {
        using Signature = typename detail::signature_convert<void(Args...)>::type;
        using ArgsTuple = ct::args_t<Signature>;
        using SigType = sig_type<Signature>;

        ReadLock<MutexPolicy> lck{mtx_};
        auto typname = CC_TYPENAME(ArgsTuple);
        // No subscribers for this topic: publish-and-drop. Standard pub/sub
        // semantics — a publisher neither knows nor cares whether subscribers
        // exist, so missing topics are intentionally ignored.
        if (auto it = signals_.find(topic); it != signals_.end()) {
            if (it->second.typname != typname) {
                throw TypeMismatchError(it->second.typname, typname);
            }
            auto* s = static_cast<SigType*>(it->second.sig.get());
            Expects(s);
            (*s)(std::forward<Args>(args)...);
        }
    }
};

/// Single-threaded signal (default, no locking).
using Signal = SignalImpl<>;

#if CC_USE_MUTEX
/// Thread-safe signal: uses recursive_mutex, supports reentrant emit/connect from callbacks.
using ConcurrentSignal = SignalImpl<std::recursive_mutex>;
#endif

}  // namespace cc
