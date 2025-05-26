#include <map>
#include <string>
#include <cc/json.h>
#include <gtest/gtest.h>

static std::string car_json = R"(
    { "make": "Toyota", "model": "Camry",  "year": 2018, "tire_pressure": [ 40.1, 39.9, 37.7, 40.4 ] }
)";

struct car_t {
    std::string make;
    std::string model;
    int year;
    std::vector<double> tire_pressure;
    std::optional<std::string> owner;
};

TEST(yyjson, encode) {
    auto car = cc::json::parse<car_t>(car_json);
    EXPECT_TRUE(car.make == "Toyota");
    EXPECT_TRUE(car.model == "Camry");
    EXPECT_TRUE(car.year == 2018);
    EXPECT_TRUE(car.tire_pressure.size() == 4);
    EXPECT_TRUE(car.tire_pressure.at(0) == 40.1);
    EXPECT_TRUE(!car.owner.has_value());
}

TEST(yyjson, decode) {
    car_t car = {
        "Toyota", "Camry", 2019, {1, 2.1, 3.2, 4.3},
           std::make_optional("cc")
    };
    auto json = cc::json::dump(car);
    EXPECT_EQ(json, "{\"make\":\"Toyota\",\"model\":\"Camry\",\"year\":2019,\"tire_"
                    "pressure\":[1.0,2.1,3.2,4.3],\"owner\":\"cc\"}");
}

TEST(yyjson, tuple) {
    std::string json = R"([1,3.5,"hello"])";
    using tuple_t    = std::tuple<int, float, std::string>;
    auto t           = cc::json::parse<tuple_t>(json);
    int x;
    float y;
    std::string z;
    std::tie(x, y, z) = t;
    EXPECT_EQ(x, 1);
    EXPECT_EQ(y, 3.5);
    EXPECT_EQ(z, "hello");

    EXPECT_EQ(cc::json::dump(t), json);
}
