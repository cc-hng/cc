#include <cc/signal.h>
#include <gtest/gtest.h>

using cc::ConcurrentSignal;
using cc::Signal;

TEST(SignalTest, ConnectAndEmit) {
    Signal sig;
    int result = 0;

    sig.connect("/add", [&](int a, int b) { result = a + b; });
    sig.emit("/add", 3, 4);

    EXPECT_EQ(7, result);
}

TEST(SignalTest, MultipleSlots) {
    Signal sig;
    int sum = 0;
    int product = 0;

    sig.connect("/op", [&](int a, int b) { sum = a + b; });
    sig.connect("/op", [&](int a, int b) { product = a * b; });
    sig.emit("/op", 3, 4);

    EXPECT_EQ(7, sum);
    EXPECT_EQ(12, product);
}

TEST(SignalTest, Disconnect) {
    Signal sig;
    int count = 0;

    auto conn = sig.connect("/topic", [&] { ++count; });
    sig.emit("/topic");
    EXPECT_EQ(1, count);

    conn.disconnect();
    sig.emit("/topic");
    EXPECT_EQ(1, count);  // no change after disconnect
}

TEST(SignalTest, EmitNonExistentTopicIsNoOp) {
    Signal sig;
    EXPECT_NO_THROW(sig.emit("/nonexistent", 1, 2, 3));
}

TEST(SignalTest, TypeMismatchThrows) {
    Signal sig;

    sig.connect("/topic", [](int) {});

    // Different signature should throw
    EXPECT_THROW(sig.emit("/topic", "hello"), cc::TypeMismatchError);
}

TEST(ConcurrentSignalTest, BasicEmit) {
    ConcurrentSignal sig;
    int val = 0;

    sig.connect("/topic", [&](int x) { val = x; });
    sig.emit("/topic", 42);

    EXPECT_EQ(42, val);
}
