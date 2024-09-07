#include <cc/value.h>
#include <gtest/gtest.h>

#include <iostream>

TEST(value, primitive) {
    cc::Value v;

    v.set<int>(1);
    EXPECT_TRUE(1 == v.get<int>());

    v.set<bool>(true);
    EXPECT_TRUE(v.get<bool>());

    v.set("hello");
    EXPECT_TRUE(v.get<std::string_view>() == "hello");

    std::vector<int> vi = {1, 2, 3};
    auto v2             = vi;
    v.set(std::move(v2));
    EXPECT_TRUE(vi == v.get<std::vector<int>>());

    std::unordered_map<std::string, int> m = {
        {"a", 1},
        {"b", 2}
    };
    v.set(m);
    EXPECT_TRUE(m == v.get<decltype(m)>());
}

TEST(value, tuple) {
    auto t = std::make_tuple<int, std::string>(1, "x");
    cc::Value v;
    v.set(t);
    EXPECT_TRUE(v[0].get<int>() == 1);
    EXPECT_TRUE(v[1].get<std::string>() == "x");

    auto t2 = v.get<std::tuple<int, std::string>>();
    EXPECT_TRUE(t == t2);
}

TEST(value, update) {
    cc::Value v1, v2;
    std::unordered_map<std::string, int> m = {
        {"a", 1},
        {"b", 2},
    };
    v1.set(m);
    std::unordered_map<std::string, int> n = {
        {"b", 3},
        {"c", 4},
    };
    v2.set(n);
    v1.update(v2);
    EXPECT_TRUE(v1.get<int>("a") == 1);
    EXPECT_TRUE(v1.get<int>("b") == 3);
    EXPECT_TRUE(v1["c"].is_null());
}

TEST(value, merge) {
    cc::Value v1(1);
    cc::Value v2(2);
    cc::Value v3;
    auto v = cc::Value::merge(v1, v2);
    EXPECT_TRUE(v.get<int>() == 2);
    v = cc::Value::merge(v1, v3);
    EXPECT_TRUE(v.get<int>() == 1);
    v = cc::Value::merge(v3, v1);
    EXPECT_TRUE(v.get<int>() == 1);

    v1["a"] = 3;
    v2["a"] = 4;
    v2["b"] = "x";
    v       = cc::Value::merge(v1, v2);
    EXPECT_TRUE(v.get<int>("a") == 4);
    EXPECT_TRUE(v.get<std::string>("b") == "x");
}

TEST(value, deepequal) {
    cc::Value v1, v2, v3;
    v1.set("a.b", 1);
    v1.set("a.c", 2);

    v2.set("a.b", 1);
    v2.set("a.c", 2);

    EXPECT_TRUE(v1.deepequal(v2));
    EXPECT_TRUE(!v1.deepequal(v3));
}
