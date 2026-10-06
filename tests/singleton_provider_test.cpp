#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <cc/singleton_provider.h>
#include <gtest/gtest.h>

// Every test uses its own type: SingletonProvider static members are process-wide,
// and ctest runs tests in random order, so sharing a type would leak state.

class NonDefaultCtor {
public:
    int v_;
    explicit NonDefaultCtor(int x) : v_(x) {}
    NonDefaultCtor() = delete;
    NonDefaultCtor(const NonDefaultCtor&) = delete;
    NonDefaultCtor& operator=(const NonDefaultCtor&) = delete;
};

TEST(SingletonProviderTest, InstanceBeforeInitThrowsThenInitWorks) {
    using P = cc::SingletonProvider<NonDefaultCtor>;
    EXPECT_THROW(P::instance(), std::runtime_error);
    P::init(42);
    EXPECT_EQ(42, P::instance().v_);
    // init is idempotent: a second call is ignored
    P::init(99);
    EXPECT_EQ(42, P::instance().v_);
}

class SlowInit {
public:
    static inline std::promise<void> started_;
    static inline std::promise<void> release_;
    int v_;
    explicit SlowInit(int x) : v_(x) {
        started_.set_value();
        release_.get_future().wait();
    }
    SlowInit() = delete;
    SlowInit(const SlowInit&) = delete;
    SlowInit& operator=(const SlowInit&) = delete;
};

TEST(SingletonProviderTest, InstanceBlocksWhileInitInFlight) {
    using P = cc::SingletonProvider<SlowInit>;
    std::thread init_thread([] { P::init(5); });
    SlowInit::started_.get_future().wait();  // constructor entered, once_flag held

    int got = -1;
    std::atomic<bool> about_to_call{false};
    std::thread instance_thread([&] {
        about_to_call = true;
        got = P::instance().v_;
    });
    while (!about_to_call) {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // still blocked inside call_once, not returned early
    EXPECT_EQ(-1, got);

    SlowInit::release_.set_value();
    init_thread.join();
    instance_thread.join();
    EXPECT_EQ(5, got);
}

class ThrowingCtor {
public:
    static inline int attempts_ = 0;
    int v_;
    explicit ThrowingCtor(int x) : v_(x) {
        if (attempts_++ < 2) {
            throw std::runtime_error("boom");
        }
    }
    ThrowingCtor() = delete;
    ThrowingCtor(const ThrowingCtor&) = delete;
    ThrowingCtor& operator=(const ThrowingCtor&) = delete;
};

TEST(SingletonProviderTest, ThrowingConstructorRetries) {
    using P = cc::SingletonProvider<ThrowingCtor>;
    EXPECT_THROW(P::init(1), std::runtime_error);
    EXPECT_THROW(P::init(2), std::runtime_error);  // once_flag unset on throw, retried
    P::init(3);
    EXPECT_EQ(3, P::instance().v_);
}

class DefaultCtorType {
public:
    int v_ = 7;
    DefaultCtorType() = default;
    DefaultCtorType(const DefaultCtorType&) = delete;
    DefaultCtorType& operator=(const DefaultCtorType&) = delete;
};

TEST(SingletonProviderTest, InstanceAutoConstructsDefault) {
    using P = cc::SingletonProvider<DefaultCtorType>;
    EXPECT_EQ(7, P::instance().v_);
}
