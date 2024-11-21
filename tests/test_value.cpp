#include <boost/hana.hpp>
#include <cc/value.h>
#include <gtest/gtest.h>

TEST(value, primitive) {
    var_t v;

    v = 1;
    EXPECT_TRUE(1 == static_cast<int>(v));

    v = true;
    EXPECT_TRUE(static_cast<bool>(v));

    v = "hello";
    EXPECT_TRUE(static_cast<std::string_view>(v) == "hello");

    std::vector<int> vi = {1, 2, 3};
    v                   = vi;
    EXPECT_TRUE(vi == static_cast<std::vector<int>>(v));

    std::unordered_map<std::string, int> m = {
        {"a", 1},
        {"b", 2}
    };
    v = m;
    EXPECT_TRUE(m == static_cast<decltype(m)>(v));
}

TEST(value, tuple) {
    auto t = std::make_tuple<int, std::string>(1, "x");
    var_t v;
    v        = t;
    auto arr = *v.as_array();
    EXPECT_TRUE(arr[0].cast<int>() == 1);
    EXPECT_TRUE(arr[1].cast<std::string>() == "x");

    auto t2 = static_cast<std::tuple<int, std::string>>(v);
    EXPECT_TRUE(t == t2);
}

TEST(value, tuple_with_value) {
    auto t = std::make_tuple<var_t, var_t>(1, true);
    var_t v;
    v        = t;
    auto arr = *v.as_array();
    EXPECT_TRUE(arr[0].cast<int>() == 1);
    EXPECT_TRUE(arr[1].cast<int>() == 1);
    EXPECT_TRUE(arr[1].cast<bool>());
}

TEST(value, patch) {
    var_t v1 = std::unordered_map<std::string, int>{
        {"a", 1},
        {"b", 2},
    };
    var_t v2 = std::unordered_map<std::string, int>{
        {"b", 3},
        {"c", 4},
    };

    var::patch(v1, v2);
    EXPECT_TRUE(var::get<int>(v1, "a") == 1);
    EXPECT_TRUE(var::get<int>(v1, "b") == 3);
    EXPECT_TRUE(!var::contains(v1, "c"));
}

TEST(value, deepequal) {
    var_t v1, v2, v3;
    var::set(v1, "a.b", 1);
    var::set(v1, "a.c", 2);

    var::set(v2, "a.b", 1);
    var::set(v2, "a.c", "2");

    var::set(v3, "a.b", 1);
    var::set(v3, "a.c.x", 1);
    EXPECT_TRUE(var::equal(v1, v2));
    EXPECT_TRUE(var::equal(v2, v1));
    EXPECT_TRUE(!var::equal(v3, v1));
}

TEST(value, remove) {
    var_t v;
    var::set(v, "a.x", 1);
    var::set(v, "a.y", 1);
    var::set(v, "b", 3);

    var::remove(v, "a.b.c");
    EXPECT_TRUE(var::contains(v, "a.x") && var::contains(v, "a.y") && var::contains(v, "b"));
    var::remove(v, "e");
    var::remove(v, "b");
    EXPECT_TRUE(var::contains(v, "a.x") && var::contains(v, "a.y") && !var::contains(v, "b"));
    var::remove(v, "a.x");
    EXPECT_TRUE(!var::contains(v, "a.x") && var::contains(v, "a.y") && !var::contains(v, "b"));
}

TEST(value, struct) {
    // clang-format off
    struct user_t {
        std::string name;
        int age;

        bool operator==(const user_t& rhs) const {
            return name == rhs.name && age == rhs.age;
        }
    };
    // clang-format on

    user_t usr = {.name = "cc", .age = 18};
    var_t v    = usr;

    user_t usr2 = var::as<user_t>(v);
    EXPECT_TRUE(usr == usr2);
}
