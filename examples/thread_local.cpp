#include <iostream>
#include <thread>

class Foo {
public:
    Foo(int i) { std::cout << "Foo::Foo() " << i << std::endl; }
    ~Foo() { std::cout << "Foo::~Foo()" << std::endl; }
};

Foo& get_foo() {
    static thread_local Foo foo(1);
    return foo;
}

void get_foo2() { thread_local Foo foo(2); }

#include <string>

static std::string a = R"(
1234567890
abcdefghijklmnopqrstuvwxyz
ABCDEFGHIJKLMNOPQRSTUVWXYZ
)";

int main() {
    std::thread t1([] { /* auto& foo = */ get_foo(); });
    std::thread t2([] {
        // auto& foo = get_foo();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "----------------------" << std::endl;
        get_foo2();
        std::cout << "----------------------" << std::endl;
    });

    t1.join();
    t2.join();
    return 0;
}
