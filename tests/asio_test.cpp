#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

// pi-lens-ignore: header-not-found
#include <cc/asio.h>
// pi-lens-ignore: header-not-found
#include <boost/asio/bind_cancellation_slot.hpp>
// pi-lens-ignore: header-not-found
#include <boost/asio/cancellation_signal.hpp>
#include <gtest/gtest.h>

namespace net = boost::asio;

namespace {

static_assert(std::is_same_v<cc::detail::task_inner_t<net::awaitable<int, net::io_context::executor_type>>, int>);

class MoveOnlyValue {
public:
    explicit MoveOnlyValue(int value) : value_(value) {}
    MoveOnlyValue(const MoveOnlyValue&) = delete;
    MoveOnlyValue& operator=(const MoveOnlyValue&) = delete;
    MoveOnlyValue(MoveOnlyValue&&) = default;
    MoveOnlyValue& operator=(MoveOnlyValue&&) = default;

    int value_;
};

class GateMutex {
public:
    static void arm() {
        std::lock_guard lock(gate_mutex_);
        armed_ = true;
        reached_ = false;
        proceed_ = false;
    }

    static void waitUntilReached() {
        std::unique_lock lock(gate_mutex_);
        gate_cv_.wait(lock, [] { return reached_; });
    }

    static void proceed() {
        {
            std::lock_guard lock(gate_mutex_);
            proceed_ = true;
        }
        gate_cv_.notify_all();
    }

    void lock() { mutex_.lock(); }
    bool try_lock() { return mutex_.try_lock(); }

    void unlock() {
        mutex_.unlock();

        std::unique_lock lock(gate_mutex_);
        if (armed_) {
            armed_ = false;
            reached_ = true;
            gate_cv_.notify_all();
            gate_cv_.wait(lock, [] { return proceed_; });
        }
    }

private:
    inline static std::mutex gate_mutex_;
    inline static std::condition_variable gate_cv_;
    inline static bool armed_ = false;
    inline static bool reached_ = false;
    inline static bool proceed_ = false;

    std::mutex mutex_;
};

net::awaitable<bool> before_timeout(net::awaitable<bool> operation) {
    using namespace net::experimental::awaitable_operators;
    auto result = co_await (std::move(operation) || cc::timeout(100));
    co_return result.index() == 0 && std::get<0>(result);
}

template <typename Trigger>
bool run_gate_probe(net::awaitable<bool> operation, Trigger&& trigger) {
    GateMutex::arm();

    net::io_context io;
    std::promise<bool> result_promise;
    auto result_future = result_promise.get_future();
    net::co_spawn(io, std::move(operation), [&result_promise](std::exception_ptr error, bool result) {
        result_promise.set_value(error == nullptr && result);
    });

    std::thread runner([&io] { io.run(); });
    GateMutex::waitUntilReached();
    std::forward<Trigger>(trigger)();
    GateMutex::proceed();

    const bool completed = result_future.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    const bool result = completed && result_future.get();
    io.stop();
    runner.join();
    return result;
}

net::awaitable<bool> receive_mpsc(cc::chan::Receiver<int> receiver) {
    auto values = co_await (*receiver)();
    co_return values.size() == 1 && values.front() == 42;
}

net::awaitable<bool> receive_closed_mpsc(cc::chan::Receiver<int> receiver) {
    auto values = co_await (*receiver)();
    co_return values.empty();
}

net::awaitable<bool> receive_oneshot(cc::chan::detail::OneshotContext<int, GateMutex>* channel) {
    co_return co_await channel->recv() == 42;
}

net::awaitable<int> receive_move_only_oneshot(cc::chan::detail::OneshotContext<MoveOnlyValue>* channel) {
    auto value = co_await channel->recv();
    co_return value.value_;
}

net::awaitable<bool> acquire_semaphore(cc::Semaphore<GateMutex>* semaphore) {
    co_await semaphore->acquire();
    co_return true;
}

}  // namespace

TEST(AsyncChannelTest, MpscDoesNotLoseSendBeforeWaitRegistration) {
    auto [sender, receiver] = cc::chan::make_mpsc<int, GateMutex>();
    auto operation = before_timeout(receive_mpsc(std::move(receiver)));

    EXPECT_TRUE(run_gate_probe(std::move(operation), [&] { (*sender)(42); }));
}

TEST(AsyncChannelTest, MpscDoesNotLoseCloseBeforeWaitRegistration) {
    auto [sender, receiver] = cc::chan::make_mpsc<int, GateMutex>();
    auto operation = before_timeout(receive_closed_mpsc(std::move(receiver)));

    EXPECT_TRUE(run_gate_probe(std::move(operation), [&] { sender.reset(); }));
}

TEST(AsyncChannelTest, OneshotDoesNotLoseSendBeforeWaitRegistration) {
    auto channel = cc::chan::make<int, GateMutex>();
    auto operation = before_timeout(receive_oneshot(channel.get()));

    EXPECT_TRUE(run_gate_probe(std::move(operation), [&] { channel->send(42); }));
}

TEST(AsyncChannelTest, SemaphoreDoesNotLoseReleaseBeforeWaitRegistration) {
    cc::Semaphore<GateMutex> semaphore(0);
    auto operation = before_timeout(acquire_semaphore(&semaphore));

    EXPECT_TRUE(run_gate_probe(std::move(operation), [&] { semaphore.release(); }));
}

// Regression: a real release while the waiter is already registered used to complete
// the timer wait with operation_aborted, making acquire() throw "Operation canceled"
// and dropping the permit. The gate-based tests above never reached that path.
TEST(AsyncChannelTest, SemaphoreAcquireCompletesAfterRealNotify) {
    cc::Semaphore<> semaphore(0);
    net::io_context io;
    std::promise<bool> result_promise;
    auto result_future = result_promise.get_future();
    net::co_spawn(
        io,
        [&]() -> net::awaitable<bool> {
            co_await semaphore.acquire();
            co_return true;
        },
        [&result_promise](std::exception_ptr error, bool result) {
            result_promise.set_value(error == nullptr && result);
        });
    net::post(io, [&] { semaphore.release(); });
    io.run();
    EXPECT_TRUE(result_future.get());
}

TEST(AsyncChannelTest, CondVarWaitUntilReturnsTrueAfterNotify) {
    cc::CondVar<> cv;
    net::io_context io;
    std::promise<bool> result_promise;
    auto result_future = result_promise.get_future();
    net::co_spawn(
        io, [&]() -> net::awaitable<bool> { co_return co_await cv.waitUntil(10000); },
        [&result_promise](std::exception_ptr error, bool result) {
            result_promise.set_value(error == nullptr && result);
        });
    net::post(io, [&] { cv.notifyAll(); });
    io.run();
    EXPECT_TRUE(result_future.get());
}

TEST(AsyncChannelTest, OneshotSupportsMoveOnlyNonDefaultConstructibleValues) {
    auto channel = cc::chan::make<MoveOnlyValue>();
    channel->send(MoveOnlyValue{42});

    net::io_context io;
    std::promise<int> result;
    auto future = result.get_future();
    net::co_spawn(io, receive_move_only_oneshot(channel.get()), [&result](std::exception_ptr error, int value) {
        result.set_value(error == nullptr ? value : -1);
    });
    io.run();

    ASSERT_EQ(future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_EQ(future.get(), 42);
}

TEST(AsyncChannelTest, RunRethrowsHandlerExceptionAfterJoiningWorkers) {
    cc::AsioPool pool;
    pool.setTimeout(0, [] { throw std::runtime_error("callback error"); });

    EXPECT_THROW(pool.run(2), std::runtime_error);
}

TEST(AsyncChannelTest, SetTimeoutDoesNotMoveLvalueCallback) {
    cc::AsioPool pool;
    int calls = 0;
    class Callback {
    public:
        int* calls_;
        int state_ = 7;

        Callback(int* calls) : calls_(calls) {}
        Callback(const Callback&) = default;
        Callback(Callback&& other) noexcept : calls_(other.calls_), state_(other.state_) { other.state_ = -1; }
        void operator()() const { ++*calls_; }
    } callback(&calls);

    auto timer = pool.setTimeout(0, callback);
    pool.run(1);

    EXPECT_EQ(callback.state_, 7);
    EXPECT_EQ(calls, 1);
}

TEST(AsyncChannelTest, SetIntervalRequiresPositiveDuration) {
    cc::AsioPool pool;
    auto callback = [](std::shared_ptr<net::steady_timer>) {};
    EXPECT_THROW(pool.setInterval(0, callback), std::invalid_argument);
    EXPECT_THROW(pool.setInterval(-1, callback), std::invalid_argument);
}

TEST(AsyncChannelTest, ClearIntervalStopsRescheduling) {
    cc::AsioPool pool;
    int calls = 0;
    auto timer = pool.setInterval(1, [&pool, &calls](std::shared_ptr<net::steady_timer> raw) {
        ++calls;
        pool.clearInterval(raw);
    });

    pool.run(1);

    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(timer.expired());
}

TEST(AsyncChannelTest, MpscDrainsPendingDataAfterClose) {
    auto [sender, receiver] = cc::chan::make_mpsc<int>();
    (*sender)(1);
    (*sender)(2);
    sender.reset();  // last sender dropped -> final_action closes the channel

    net::io_context io;
    std::vector<int> got;
    bool done = false;
    net::co_spawn(
        io,
        [&]() -> net::awaitable<void> {
            auto values = co_await (*receiver)();
            got.assign(values.begin(), values.end());
            done = true;
        },
        [](std::exception_ptr) {});
    io.run();

    EXPECT_TRUE(done);
    EXPECT_EQ(got, (std::vector<int>{1, 2}));
}

TEST(AsyncChannelTest, MpscRejectsSendAfterClosedChannel) {
    cc::chan::detail::MpscContext<int> ctx;
    ctx.close();
    EXPECT_THROW(ctx.send(1), std::runtime_error);
}

TEST(AsyncChannelTest, OneshotChannelSurvivesCancelledWait) {
    auto channel = cc::chan::make<int>();
    net::io_context io;
    net::cancellation_signal sig;
    bool cancelled = false;
    net::co_spawn(
        io,
        [&]() -> net::awaitable<void> {
            try {
                co_await channel->recv();
            } catch (const boost::system::system_error& e) {
                if (e.code() != net::error::operation_aborted) {
                    throw;
                }
                cancelled = true;
            }
        },
        net::bind_cancellation_slot(sig.slot(), [](std::exception_ptr) {}));
    // Keep io.run() alive across the cancellation: once the cancelled wait unwinds
    // there is no outstanding work, and without the guard run() would return before
    // the second recv is posted.
    auto work = net::make_work_guard(io);
    std::thread runner([&io] { io.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // recv registered
    // Emit on the io thread: cancellation_signal::emit is not thread-safe and
    // delivery is only guaranteed where the wait is registered.
    net::post(io, [&sig] { sig.emit(net::cancellation_type::all); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // cancel processed

    channel->send(42);
    std::promise<int> result;
    auto future = result.get_future();
    net::co_spawn(
        io, [&]() -> net::awaitable<void> { result.set_value(co_await channel->recv()); },
        [](std::exception_ptr) {});
    const bool ready = future.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    io.stop();
    runner.join();

    ASSERT_TRUE(cancelled);
    ASSERT_TRUE(ready);
    EXPECT_EQ(future.get(), 42);
}
