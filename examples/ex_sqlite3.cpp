#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <cc/sqlite3.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

// clang-format off
struct user_t {
    BOOST_HANA_DEFINE_STRUCT(user_t,
        (std::optional<int>, id),
        (std::string, name),
        (int, age));
};

struct cnt_result_t {
    BOOST_HANA_DEFINE_STRUCT(cnt_result_t,
        (int, cnt));
};

struct user_query_option_t {
  std::optional<std::string> name;
};

BOOST_HANA_ADAPT_STRUCT(user_query_option_t, name);
// clang-format on

int main() {
    fmt::print("version: {}\n", sqlite3_version);

    cc::Sqlite3pp<std::mutex> sqlconn;

    sqlconn.open(":memory:");

    sqlconn.execute(R"(
        CREATE TABLE IF NOT EXISTS user (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            age INTEGER
        );
    )");

    sqlconn.execute(R"(INSERT INTO user (name, age) VALUES ('Alice', 30))");
    sqlconn.execute(R"(INSERT INTO user (name, age) VALUES ('Bob', 25))");
    sqlconn.execute(R"(INSERT INTO user (name, age) VALUES ('Charlie', 35))");

    std::string name = "Alice";
    int age          = 30;
    auto r5          = sqlconn.execute<std::tuple<int>>(
        "select count(*) as cnt from user where age = ? and name = ?", age, name);
    fmt::print("cnt: {}\n", std::get<0>(r5.at(0)));

    auto r1 = sqlconn.execute<cnt_result_t>("select count(*) as cnt from user");
    fmt::print("cnt: {}\n", r1.at(0).cnt);

    fmt::print("-------------------------------------------- 2\n");
    user_query_option_t opt{.name = "Bob"};
    auto ret = sqlconn.execute<user_t>("select * from user where (? IS NULL OR name = ?)",
                                       opt.name, opt.name);
    for (const auto& usr : ret) {
        fmt::print("id: {}, name: {}, age: {}\n", usr.id.value(), usr.name, usr.age);
    }

    fmt::print("--------------------------------------------\n");
    using user_row_t = std::tuple<int, std::string, int>;
    auto r2          = sqlconn.execute<user_row_t>("select * from user");
    for (const auto& [id, name, age] : r2) {
        fmt::print("id: {}, name: {}, age: {}\n", id, name, age);
    }

    sqlconn.close();
    return 0;
}
