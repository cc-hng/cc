#ifdef CC_WITH_SQLITE3

#    include "common.h"
#    include <cc/singleton_provider.h>
#    include <cc/sqlite3pp.h>
#    include <fmt/core.h>

struct user_t {
    std::string name;
    int age;
};

using Sqlite3Provider = cc::SingletonProvider<cc::Sqlite3pp>;

static void init_sqlite3() {
    Sqlite3Provider::init(":memory:");
    auto& kSqlConn = Sqlite3Provider::instance();

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
    auto& kSqlConn = Sqlite3Provider::instance();
    b.title("sqlite3");
    b.run("query", [&] {
        auto r = kSqlConn.execute<user_t>("select * from user where name=?", "A333");
        bench::doNotOptimizeAway(r);
    });
    b.run("insert",
          [&] { kSqlConn.execute("insert into user(name, age) values (?, ?)", "B111", 111); });
}

BENCHMARK_REGISTE(bench_sqlite3);

#endif
