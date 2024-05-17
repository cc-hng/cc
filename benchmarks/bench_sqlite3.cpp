#include "common.h"
#include <boost/hana.hpp>
#include <cc/sqlite3.h>
#include <fmt/core.h>

// clang-format off
struct user_t {
    BOOST_HANA_DEFINE_STRUCT(user_t,
        (std::string, name),
        (int, age));
};
// clang-format on

static cc::Sqlite3pp<> kSqlConn;

static void init_sqlite3() {
    kSqlConn.open(":memory:");

    kSqlConn.execute(R"(
        CREATE TABLE user (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            age INTEGER
        );
    )");
    kSqlConn.execute("CREATE INDEX idx_user_name ON user(name);");

    kSqlConn.execute("BEGIN");
    for (int i = 0; i < 100000; i++) {
        std::string name = fmt::format("A{}", i + 1);
        int age          = i % 100 + 1;
        kSqlConn.execute("INSERT INTO user(name, age) VALUES (?, ?)", name, age);
    }
    kSqlConn.execute("COMMIT");
}

CC_CALL_OUTSIDE(init_sqlite3());

static void bench_sqlite3(bench::Bench& b) {
    b.title("sqlite3");
    b.run("query", [&] {
        auto r = kSqlConn.execute<user_t>("select * from user where name=?", "A333");
        bench::doNotOptimizeAway(r);
    });
    b.run("insert", [&] {
        kSqlConn.execute("insert into user(name, age) values (?, ?)", "B111", 111);
    });
}

BENCHMARK_REGISTE(bench_sqlite3);
