#ifdef CC_WITH_YYJSON

#    include "cc/json.h"
#    include <map>
#    include <string>
#    include <boost/hana.hpp>
#    include <gtest/gtest.h>

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

BOOST_HANA_ADAPT_STRUCT(car_t, make, model, year, tire_pressure, owner);

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

TEST(yyjson, map_key_mem) {
    auto doc = yyjson_mut_doc_new(NULL);

    {
        std::map<std::string, int> persons = {
            {"cc",   32},
            {"alan", 64},
        };
        auto root = cc::json::detail::yyjson_convert<decltype(persons)>::to_json(doc, persons);
        yyjson_mut_doc_set_root(doc, root);
    }

    yyjson_write_err err;
    auto js = yyjson_mut_write_opts(doc, 0, NULL, NULL, &err);
    ::free(js);
    yyjson_mut_doc_free(doc);
    EXPECT_EQ(err.code, 0);
}

TEST(yyjson, yyjson_val) {
    cc::json::JsonParser parser(car_json, false);

    auto root = parser.parse<yyjson_val*>();
    EXPECT_TRUE(yyjson_is_obj(root));
    EXPECT_NE(yyjson_obj_get(root, "make"), nullptr);
    EXPECT_NE(yyjson_obj_get(root, "model"), nullptr);
    EXPECT_NE(yyjson_obj_get(root, "year"), nullptr);
    EXPECT_NE(yyjson_obj_get(root, "tire_pressure"), nullptr);
    EXPECT_EQ(yyjson_obj_get(root, "owner"), nullptr);

    using M = std::unordered_map<std::string, yyjson_val*>;
    auto m  = parser.parse<M>();
    EXPECT_EQ(m.count("make"), 1);
    EXPECT_EQ(m.count("model"), 1);
    EXPECT_EQ(m.count("year"), 1);
    EXPECT_EQ(m.count("tire_pressure"), 1);
    EXPECT_EQ(m.count("owner"), 0);
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

#endif
