
#include "common.h"
#include <cc/singleton_provider.h>
#include <cc/sqlite3pp.h>
#include <fmt/core.h>
#include <fmt/format.h>

struct user_t {
    std::string name;
    int age;
};

using Sqlite3Provider = cc::SingletonProvider<cc::Sqlite3Session>;

static void init_sqlite3() {
    Sqlite3Provider::init(":memory:");
    // Sqlite3Provider::init("/dev/shm/my.sqlite");
    auto& sqlconn = Sqlite3Provider::instance();

    sqlconn->execute(R"(
        CREATE TABLE IF NOT EXISTS user (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            age INTEGER

        );
    )");
    sqlconn->execute("CREATE INDEX IF NOT EXISTS idx_user_name ON user(name);");

    sqlconn->beginTransaction();
    for (int i = 0; i < 100000; i++) {
        std::string name = fmt::format("A{}", i + 1);
        int age = i % 100 + 1;
        sqlconn->execute("INSERT INTO user(name, age) VALUES (?, ?)", name, age);
    }
    sqlconn->endTransaction();
}

CC_CALL_OUTSIDE(init_sqlite3());

static void bench_sqlite3(bench::Bench& b) {
    auto& sqlconn = Sqlite3Provider::instance();
    b.run("query", [&] {
        auto r = sqlconn->executeOnce<user_t>("select name, age from user where name=?", "A333");
        bench::doNotOptimizeAway(r);
    });
    b.run("query(cache)", [&] {
        auto r = sqlconn->execute<user_t>("select name, age from user where name=?", "A333");
        bench::doNotOptimizeAway(r);
    });
    b.run("insert", [&] { sqlconn->executeOnce("insert into user(name, age) values (?, ?)", "B111", 111); });
    b.run("insert(cache)", [&] { sqlconn->execute("insert into user(name, age) values (?, ?)", "B111", 111); });
}

BENCHMARK_REGISTE("sqlite3", bench_sqlite3, 5120);
