
#include "common.h"
#include <cc/json.h>
#include <cc/value.h>

static std::string car_json = R"(
{
  "make": "Toyota",
  "model": "Camry",
  "year": 2018,
  "owner": { "name": "cc", "age": 40 },
  "tire_pressure": [ 40.1, 39.9, 37.7, 40.4 ]
}
)";

struct user_t {
    std::string name;
    int age;

    bool operator==(const user_t& rhs) const { return name == rhs.name && age == rhs.age; }
};

struct car_t {
    std::string make;
    std::string model;
    int year;
    std::vector<double> tire_pressure;
    user_t owner;

    bool operator==(const car_t& rhs) const {
        return make == rhs.make && model == rhs.model && year == rhs.year
               && tire_pressure == rhs.tire_pressure && owner == rhs.owner;
    }
};

static void bench_value(bench::Bench& b) {
    car_t car1 = cc::json::parse<car_t>(car_json);
    car_t car2 = cc::json::parse<car_t>(car_json);
    var_t v1   = car1;
    var_t v2   = car2;

    b.title("value");
    b.run("struct::deepcopy", [&] {
        car_t car3 = car1;
        bench::doNotOptimizeAway(car3);
    });
    b.run("value::deepcopy", [&] {
        var_t v3 = var::clone(v1);
        bench::doNotOptimizeAway(v3);
    });

    b.run("struct::deepequal", [&] {
        auto b = (car1 == car2);
        bench::doNotOptimizeAway(b);
    });
    b.run("value::deepequal", [&] {
        auto b = var::equal(v1, v2);
        bench::doNotOptimizeAway(b);
    });
}

BENCHMARK_REGISTE(bench_value);

static inline int add1(int a, int b) {
    return a + b;
}

static constexpr auto add2 = [](int a, int b) -> int {
    return a + b;
};

static void bench_function_value(bench::Bench& b) {
    var_t params = std::make_tuple(3, 4);

    b.title("value");
    b.run("add1:raw", [&] {
        auto sum = add1(3, 4);
        bench::doNotOptimizeAway(sum);
    });
    b.run("add2:raw", [&] {
        auto sum = add2(3, 4);
        bench::doNotOptimizeAway(sum);
    });
    b.run("add1:value", [&] {
        auto [a, b] = var::as<std::tuple<int, int>>(params);
        auto sum    = add1(a, b);
        bench::doNotOptimizeAway(sum);
    });
    b.run("add2:value", [&] {
        auto [a, b] = var::as<std::tuple<int, int>>(params);
        auto sum    = add2(a, b);
        bench::doNotOptimizeAway(sum);
    });
}

BENCHMARK_REGISTE(bench_function_value);
