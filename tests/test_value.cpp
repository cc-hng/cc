#include <boost/hana.hpp>
#include <cc/value.h>
#include <gtest/gtest.h>

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

TEST(value, tuple_with_value) {
    auto t = std::make_tuple<cc::Value, cc::Value>(1, true);
    cc::Value v;
    v.set(t);
    EXPECT_TRUE(v[0].get<int>() == 1);
    EXPECT_TRUE(v[1].get<int>() == 1);
    EXPECT_TRUE(v[1].get<bool>());

    auto t2 = v.get<std::tuple<cc::Value, cc::Value>>();
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
    v2.set("a.c", "2");

    v3.set("a.b", 1);
    v3.set("a.c.x", 1);

    EXPECT_TRUE(v1.deepequal(v2));
    EXPECT_TRUE(v2.deepequal(v1));
    EXPECT_TRUE(!v3.deepequal(v1));
}

TEST(value, remove) {
    cc::Value v;
    v.set("a.x", 1);
    v.set("a.y", 1);

    v.set("b", 3);

    v.remove("a.b.c");
    EXPECT_TRUE(v.contains("a.x") && v.contains("a.y") && v.contains("b"));
    v.remove("e");
    v.remove("b");
    EXPECT_TRUE(v.contains("a.x") && v.contains("a.y") && !v.contains("b"));
    v.remove("a.x");
    EXPECT_TRUE(!v.contains("a.x") && v.contains("a.y") && !v.contains("b"));
}

TEST(value, struct) {
    // clang-format off
    struct user_t {
        BOOST_HANA_DEFINE_STRUCT(user_t,
            (std::string, name),
            (int, age));
    };
    // clang-format on

    user_t usr = {.name = "cc", .age = 18};
    cc::Value v;
    v.set(usr);

    user_t usr2 = v.get<user_t>();
    EXPECT_TRUE(usr.name == usr2.name && usr.age == usr2.age);
}
