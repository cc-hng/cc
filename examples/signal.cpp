#include <cc/signal.h>
#include <cc/singleton_provider.h>
#include <fmt/core.h>

void add(int a, int b) { fmt::print("{} + {} = {}\n", a, b, a + b); }
void sub(int a, int b) { fmt::print("{} - {} = {}\n", a, b, a - b); }

using SignalProvider = cc::SingletonProvider<cc::Signal>;

int main() {
    auto& bus = SignalProvider::instance();
    bus.connect("/foo", add);
    bus.connect("/foo", sub);
    bus.emit("/foo", 4, 2);
    return 0;
}
