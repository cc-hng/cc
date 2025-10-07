#pragma once

#include <list>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <boost/core/demangle.hpp>
#include <boost/core/noncopyable.hpp>
#include <boost/stacktrace.hpp>
#include <fmt/format.h>
#include <gsl/gsl>

#ifdef __linux__
#    include <pthread.h>
#endif

#define CC_CONCAT0(a, b) a##b
#define CC_CONCAT(a, b)  CC_CONCAT0(a, b)
#define CC_CALL_OUTSIDE(fn) \
    [[maybe_unused]] static const bool CC_CONCAT(__b_, __LINE__) = ((fn), true)

#define CASSERT(cond)                                       \
    do {                                                    \
        if (GSL_UNLIKELY(!(cond))) {                        \
            std::ostringstream oss;                         \
            oss << "\n" << boost::stacktrace::stacktrace(); \
            throw std::runtime_error(oss.str());            \
        }                                                   \
    } while (0)

namespace cc {

namespace detail {

template <typename T>
struct typeone {
    static std::string name() {
        static constexpr auto str_remove = [](std::string s, std::string toremove) -> std::string {
            for (;;) {
                auto pos = s.find(toremove);
                if (pos == std::string::npos) {
                    break;
                }
                s.erase(pos, toremove.size());
            }
            return s;
        };

        if constexpr (std::is_same_v<T, std::string>) {
            return "string";
        } else if constexpr (std::is_same_v<T, std::string_view>) {
            return "string_view";
        } else if constexpr (std::is_same_v<T, const char*>) {
            return "cstr";
        } else {
            auto r = boost::core::demangle(typeid(T).name());
            return str_remove(r, "__cdecl");
        }
    }
};

template <typename T, typename A>
struct typeone<std::vector<T, A>> {
    static std::string name() { return fmt::format("vector<{}>", typeone<T>::name()); }
};

template <typename T, typename A>
struct typeone<std::list<T, A>> {
    static std::string name() { return fmt::format("list<{}>", typeone<T>::name()); }
};

template <typename V, typename C, typename A>
struct typeone<std::map<V, C, A>> {
    static std::string name() { return fmt::format("map<string, {}>", typeone<C>::name()); }
};

template <typename V, typename C, typename A>
struct typeone<std::unordered_map<V, C, A>> {
    static std::string name() { return fmt::format("hashmap<string, {}>", typeone<C>::name()); }
};

}  // namespace detail

class NonMutex {
public:
    inline void lock() {}
    inline bool try_lock() { return true; }
    inline void unlock() {}
};

template <typename Mutex>
class LockGuard : boost::noncopyable {
public:
    explicit LockGuard(Mutex& mtx) : mtx_(mtx) { mtx.lock(); }
    ~LockGuard() { mtx_.unlock(); }

private:
    Mutex& mtx_;
};

inline void set_threadname(const char* name) {
#ifdef __linux__
    pthread_setname_np(pthread_self(), name);
#endif
}

/// --- typename
template <typename... Args>
std::string type_name() {
    if constexpr (sizeof...(Args) == 0) {
        return "()";
    } else if constexpr (sizeof...(Args) == 1) {
        return detail::typeone<Args...>::name();
    } else {
        std::string r = "(";
        ((r += detail::typeone<Args>::name() + ","), ...);
        r.back() = ')';
        return r;
    }
}

}  // namespace cc
