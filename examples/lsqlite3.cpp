
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <cc/singleton_provider.h>
#include <cc/sqlite3pp.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <sqlite3.h>

using Sqlite3Provider = cc::SingletonProvider<cc::Sqlite3Session>;

struct user_t {
    std::optional<int> id;
    std::string name;
    int age;
};

struct cnt_result_t {
    int cnt;
};

struct user_query_option_t {
    std::optional<std::string> name;
};

int main() {
    fmt::print("version: {}\n", sqlite3_version);

    Sqlite3Provider::init(":memory:");
    // Sqlite3Provider::init("./db/my.sqlite3");
    // Sqlite3Provider::init("/dev/shm/my.sqlite");
    auto& sqlconn = Sqlite3Provider::instance();

    sqlconn->execute(R"(
        CREATE TABLE IF NOT EXISTS user (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            age INTEGER
        );
    )");

    sqlconn->executeOnce(R"(INSERT INTO user (name, age) VALUES ('Alice', 30))");
    sqlconn->executeOnce(R"(INSERT INTO user (name, age) VALUES ('Bob', 25))");
    sqlconn->executeOnce(R"(INSERT INTO user (name, age) VALUES ('Charlie', 35) RETURNING id)");

    std::string name = "Alice";
    int age = 30;
    auto r5 = sqlconn->execute<std::tuple<int>>("select count(*) as cnt from user where age = ? and name = ?",
                                                age, name);
    fmt::print("cnt: {}\n", std::get<0>(r5.at(0)));

    for (int i = 0; i < 10; i++) {
        auto r1 = sqlconn->execute<cnt_result_t>("select count(*) as cnt from user");
        fmt::print("{}: cnt: {}\n", i, r1.at(0).cnt);
    }

    fmt::print("-------------------------------------------- 2\n");
    user_query_option_t opt{.name = "Bob"};
    auto ret = sqlconn->execute<user_t>("select * from user where (? IS NULL OR name = ?)", opt.name, opt.name);
    for (const auto& usr : ret) {
        fmt::print("id: {}, name: {}, age: {}\n", usr.id.value(), usr.name, usr.age);
    }

    fmt::print("--------------------------------------------\n");
    using user_row_type = std::tuple<int, std::string, int>;
    auto r2 = sqlconn->execute<user_row_type>("select * from user");
    for (const auto& [id, name, age] : r2) {
        fmt::print("id: {}, name: {}, age: {}\n", id, name, age);
    }

    sqlconn->close();
    return 0;
}
