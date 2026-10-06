#include <array>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cc/nameof.h>
#include <gtest/gtest.h>

// Bind the macro result to a variable first: expanding a macro that carries
// template commas inside EXPECT_EQ re-splits on the comma (the preprocessor
// does not understand angle brackets), same limitation as upstream NAMEOF_TYPE.
// Container/tuple/pair branches must compile (they instantiate the
// ContainerLike specialization that NAMEOF_TYPE feeds), and the names must be
// free of libstdc++ noise (__cxx11, allocators).
TEST(NameofTest, TypenameContainers) {
    const auto v1 = CC_TYPENAME(std::vector<int>);
    EXPECT_EQ("std::vector<int>", v1);
    const auto v2 = CC_TYPENAME(std::vector<std::string>);
    EXPECT_EQ("std::vector<std::string>", v2);
    const auto m = CC_TYPENAME(std::map<std::string, int>);
    EXPECT_EQ("std::map<std::string, int>", m);
    const auto s = CC_TYPENAME(std::set<std::string>);
    EXPECT_EQ("std::set<std::string>", s);
    const auto d = CC_TYPENAME(std::deque<int>);
    EXPECT_EQ("std::deque<int>", d);
    const auto um = CC_TYPENAME(std::unordered_map<int, int>);
    EXPECT_EQ("std::unordered_map<int, int>", um);
}

TEST(NameofTest, TypenameNested) {
    const auto vv = CC_TYPENAME(std::vector<std::vector<int>>);
    EXPECT_EQ("std::vector<std::vector<int>>", vv);
    const auto mv = CC_TYPENAME(std::map<int, std::vector<int>>);
    EXPECT_EQ("std::map<int, std::vector<int>>", mv);
    const auto vp = CC_TYPENAME(std::vector<std::pair<int, std::string>>);
    EXPECT_EQ("std::vector<std::pair<int, std::string>>", vp);
}

TEST(NameofTest, TypenameTuplePair) {
    const auto t = CC_TYPENAME(std::tuple<int, std::string>);
    EXPECT_EQ("std::tuple<int, std::string>", t);
    const auto p = CC_TYPENAME(std::pair<int, std::string>);
    EXPECT_EQ("std::pair<int, std::string>", p);
}

TEST(NameofTest, TypenameArrayKeepsSize) {
    const auto a = CC_TYPENAME(std::array<int, 5>);
    EXPECT_EQ("std::array<int, 5>", a);
    const auto as = CC_TYPENAME(std::array<std::string, 3>);
    EXPECT_EQ("std::array<std::string, 3>", as);
}

TEST(NameofTest, TypenameFundamentalAndStrings) {
    const auto i = CC_TYPENAME(int);
    EXPECT_EQ("int", i);
    const auto st = CC_TYPENAME(std::string);
    EXPECT_EQ("std::string", st);
    const auto sv = CC_TYPENAME(std::string_view);
    EXPECT_EQ("std::string_view", sv);
}

TEST(NameofTest, TypenameExpr) {
    const std::vector<int> v;
    const auto tn = CC_TYPENAME_EXPR(v);
    EXPECT_EQ("std::vector<int>", tn);
    const auto tn2 = CC_TYPENAME_EXPR(42);
    EXPECT_EQ("int", tn2);
}
