#include <cc/function.h>

int add1(int a, int b) {
    return a + b;
}

constexpr auto add2 = [](int a, int b) {
    return a + b;
};

class AddFunctor {
public:
    int operator()(int a, int b) { return a + b; }
};

constexpr AddFunctor add3;

static std::function<int(int, int)> add4 = add2;

class Algorithm {
public:
    int add(int a, int b) { return a + b; }
};

void test_functor(const auto& f) {
    int sum = cc::invoke(f, 1, 2);
    if (sum != 3) {
        throw;
    }
}

int main() {
    Algorithm algo;

    test_functor(cc::bind(add1));
    test_functor(cc::bind(add2));
    test_functor(cc::bind(add3));
    test_functor(cc::bind(add4));
    test_functor(cc::bind(&Algorithm::add, &algo));

    return 0;
}
