#pragma once

#include <any>
#include <memory>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <boost/core/noncopyable.hpp>
#include <cc/singleton_provider.h>
#include <cc/stopwatch.h>
#include <cc/util.h>

namespace cc {
namespace st {

struct R_TRIG : boost::noncopyable {
    mutable int M = 0;

    int operator()(int CLK) const {
        auto Q = CLK && !M;
        M      = CLK;
        return Q;
    }
};

struct F_TRIG : boost::noncopyable {
    mutable int M = 1;

    int operator()(int CLK) const {
        auto Q = !CLK && M;
        M      = CLK;
        return Q;
    }
};

struct TON : boost::noncopyable {
    mutable int Q       = 0;
    mutable int ET      = 0;
    mutable int STATE   = 0;
    mutable int PREV_IN = 0;
    mutable StopWatch stopwatch;

    std::tuple<int, int> operator()(int IN, int PT) const {
        //
        if (STATE == 0 && !PREV_IN && IN) {
            STATE = 1;
            Q     = 0;
            stopwatch.reset();
        } else {
            if (!IN) {
                STATE = 0;
                Q     = 0;
                ET    = 0;
            } else if (STATE == 1) {
                int elapsed = stopwatch.elapsed() * 1000;
                if (elapsed > PT) {
                    STATE = 2;
                    Q     = 1;
                    ET    = PT;
                } else {
                    ET = elapsed;
                }
            }
        }

        PREV_IN = IN;
        return std::make_tuple(Q, ET);
    }
};

struct TOF : boost::noncopyable {
    mutable int Q       = 0;
    mutable int ET      = 0;
    mutable int STATE   = 0;  // (* internal state: 0-reset, 1-counting, 2-set *)
    mutable int PREV_IN = 0;
    mutable StopWatch stopwatch;

    std::tuple<int, int> operator()(int IN, int PT) const {
        //
        if (STATE == 0 && PREV_IN && !IN) {
            STATE = 1;
            stopwatch.reset();
        } else {
            if (IN) {
                STATE = 0;
                ET    = 0;
            } else if (STATE == 1) {
                int elapsed = stopwatch.elapsed() * 1000;
                if (elapsed > PT) {
                    STATE = 2;
                    ET    = PT;
                } else {
                    ET = elapsed;
                }
            }
        }

        Q       = IN || (STATE == 1);
        PREV_IN = IN;
        return std::make_tuple(Q, ET);
    }
};

}  // namespace st

template <typename MutexPolicy = NonMutex>
class StFactory final : boost::noncopyable {
#define ST_FACTORY_METHOD(funcname, type) \
    inline decltype(auto) funcname(std::string_view name) { return get<type>(name); }

public:
    StFactory() = default;

    ST_FACTORY_METHOD(r_trig, cc::st::R_TRIG)
    ST_FACTORY_METHOD(f_trig, cc::st::F_TRIG)
    ST_FACTORY_METHOD(ton, cc::st::TON)
    ST_FACTORY_METHOD(tof, cc::st::TOF)

private:
    template <typename T>
    const T& get(std::string_view name) {
        std::lock_guard<MutexPolicy> _lck{mtx_};
        std::string k = tname<T>(std::string(name));
        if (m_.find(k) == m_.end()) {
            auto p = std::make_shared<T>();
            m_.emplace((std::string&&)k, p);
            return *p;
        }
        auto p = std::any_cast<std::shared_ptr<T>>(m_.at(k));
        return *p;
    }

    template <typename T>
    static inline std::string tname(std::string name) {
        return std::string(typeid(T).name()) + ":" + name;
    }

private:
    MutexPolicy mtx_;
    std::unordered_map<std::string, std::any> m_;
};

using StFactoryProvider           = SingletonProvider<StFactory<>>;
using ConcurrentStFactoryProvider = SingletonProvider<StFactory<std::mutex>>;

}  // namespace cc
