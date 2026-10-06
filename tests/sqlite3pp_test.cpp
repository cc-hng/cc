#include <cstdint>
#include <filesystem>
#include <tuple>
#include <cc/sqlite3pp.h>
#include <gtest/gtest.h>

using cc::Sqlite3pp;

struct user_row_t {
    int id;
    std::string name;
    int age;
};

struct cnt_result_t {
    int cnt;
};

struct nullable_row_t {
    std::optional<int> value;
    std::optional<std::string> name;
};

struct blob_row_t {
    std::vector<char> value;
};

struct id_value_row_t {
    int id;
    int value;
};

struct required_row_t {
    int value;
};

struct optional_missing_row_t {
    int id;
    std::optional<std::string> name;
};

struct required_missing_row_t {
    int id;
};

TEST(Sqlite3ppTest, CreateAndInsert) {
    Sqlite3pp db(":memory:");

    db.execute(R"(
        CREATE TABLE IF NOT EXISTS test_user (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            age INTEGER
        )
    )");

    db.executeOnce("INSERT INTO test_user (name, age) VALUES (?, ?)", "Alice", 30);
    db.executeOnce("INSERT INTO test_user (name, age) VALUES (?, ?)", "Bob", 25);

    auto rows =
        db.execute<std::tuple<int, std::string, int>>("SELECT id, name, age FROM test_user ORDER BY id");

    ASSERT_EQ(2, rows.size());
    EXPECT_EQ(1, std::get<0>(rows[0]));
    EXPECT_EQ("Alice", std::get<1>(rows[0]));
    EXPECT_EQ(30, std::get<2>(rows[0]));

    EXPECT_EQ(2, std::get<0>(rows[1]));
    EXPECT_EQ("Bob", std::get<1>(rows[1]));
    EXPECT_EQ(25, std::get<2>(rows[1]));
}

TEST(Sqlite3ppTest, BindsMutableCString) {
    Sqlite3pp db(":memory:");
    db.execute("CREATE TABLE t (name TEXT NOT NULL)");

    char buf[] = "Carol";  // char*, not const char*
    char* p = buf;
    db.executeOnce("INSERT INTO t (name) VALUES (?)", p);

    auto rows = db.execute<std::tuple<std::string>>("SELECT name FROM t");
    ASSERT_EQ(1, rows.size());
    EXPECT_EQ("Carol", std::get<0>(rows[0]));
}

TEST(Sqlite3ppTest, BindOptionalString) {
    Sqlite3pp db(":memory:");
    db.execute("CREATE TABLE t (name TEXT)");

    std::optional<std::string> name = "Dora";
    db.executeOnce("INSERT INTO t (name) VALUES (?)", name);
    db.executeOnce("INSERT INTO t (name) VALUES (?)", std::optional<std::string>{});

    auto rows = db.execute<std::tuple<std::optional<std::string>>>("SELECT name FROM t ORDER BY rowid");
    ASSERT_EQ(2, rows.size());
    ASSERT_TRUE(std::get<0>(rows[0]).has_value());
    EXPECT_EQ("Dora", *std::get<0>(rows[0]));
    EXPECT_FALSE(std::get<0>(rows[1]).has_value());
}

TEST(Sqlite3ppTest, BindsLargeUnsignedInteger) {
    // Regression: a 4-byte unsigned value went through sqlite3_bind_int, so
    // anything above INT_MAX was silently stored as a negative number.
    Sqlite3pp db(":memory:");
    db.execute("CREATE TABLE t (v INTEGER)");

    const std::uint32_t big = 4000000000u;
    db.executeOnce("INSERT INTO t VALUES (?)", big);

    auto rows = db.execute<std::tuple<std::int64_t>>("SELECT v FROM t");
    ASSERT_EQ(1, rows.size());
    EXPECT_EQ(4000000000LL, std::get<0>(rows[0]));
}

TEST(Sqlite3ppTest, BareFilenameDoesNotCreateParentDirectory) {
    EXPECT_NO_THROW(cc::detail::mk_parent_dir("cc_s3pp_bare_path_test.sqlite3"));
}

TEST(Sqlite3ppTest, StructRowType) {
    Sqlite3pp db(":memory:");

    db.execute(R"(
        CREATE TABLE IF NOT EXISTS s_user (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            age INTEGER
        )
    )");

    db.executeOnce("INSERT INTO s_user (name, age) VALUES (?, ?)", "Charlie", 35);

    auto rows = db.execute<user_row_t>("SELECT id, name, age FROM s_user");
    ASSERT_EQ(1, rows.size());
    EXPECT_EQ(1, rows[0].id);
    EXPECT_EQ("Charlie", rows[0].name);
    EXPECT_EQ(35, rows[0].age);
}

TEST(Sqlite3ppTest, NullColumnsMapToNullopt) {
    Sqlite3pp db(":memory:");
    db.execute("CREATE TABLE t (value INTEGER, name TEXT)");
    db.execute("INSERT INTO t VALUES (NULL, NULL)");

    auto rows = db.execute<nullable_row_t>("SELECT value, name FROM t");
    ASSERT_EQ(1, rows.size());
    EXPECT_FALSE(rows[0].value.has_value());
    EXPECT_FALSE(rows[0].name.has_value());
}

TEST(Sqlite3ppTest, EmptyBlobReadsAsEmptyVector) {
    Sqlite3pp db(":memory:");
    db.execute("CREATE TABLE t (value BLOB)");
    db.execute("INSERT INTO t VALUES (x'')");

    auto rows = db.execute<blob_row_t>("SELECT value FROM t");
    ASSERT_EQ(1, rows.size());
    EXPECT_TRUE(rows[0].value.empty());
}

TEST(Sqlite3ppTest, VectorCharBindsAsBlobNotText) {
    // Regression: the bind path matched vector<char> through char_text and stored
    // it as TEXT, while the read path decoded it as BLOB.
    Sqlite3pp db(":memory:");
    db.execute("CREATE TABLE t (value BLOB)");

    std::vector<char> blob{'a', '\0', 'b'};
    db.executeOnce("INSERT INTO t (value) VALUES (?)", blob);

    auto types = db.execute<std::tuple<std::string>>("SELECT typeof(value) FROM t");
    ASSERT_EQ(1, types.size());
    EXPECT_EQ("blob", std::get<0>(types[0]));

    auto rows = db.execute<blob_row_t>("SELECT value FROM t");
    ASSERT_EQ(1, rows.size());
    ASSERT_EQ(3u, rows[0].value.size());
    EXPECT_EQ('a', rows[0].value[0]);
    EXPECT_EQ('\0', rows[0].value[1]);
    EXPECT_EQ('b', rows[0].value[2]);
}

TEST(Sqlite3ppTest, EmptyVectorCharBindsEmptyBlobNotNull) {
    Sqlite3pp db(":memory:");
    db.execute("CREATE TABLE t (value BLOB)");
    db.executeOnce("INSERT INTO t (value) VALUES (?)", std::vector<char>{});

    auto types = db.execute<std::tuple<std::string>>("SELECT typeof(value) FROM t");
    ASSERT_EQ(1, types.size());
    EXPECT_EQ("blob", std::get<0>(types[0]));

    auto rows = db.execute<blob_row_t>("SELECT value FROM t");
    ASSERT_EQ(1, rows.size());
    EXPECT_TRUE(rows[0].value.empty());
}

TEST(Sqlite3ppTest, StructColumnMapIsScopedToConnection) {
    Sqlite3pp first(":memory:");
    first.execute("CREATE TABLE t (id INTEGER, value INTEGER)");
    first.execute("INSERT INTO t VALUES (1, 2)");
    auto first_rows = first.execute<id_value_row_t>("SELECT * FROM t");
    ASSERT_EQ(1, first_rows.size());
    EXPECT_EQ(1, first_rows[0].id);
    EXPECT_EQ(2, first_rows[0].value);

    Sqlite3pp second(":memory:");
    second.execute("CREATE TABLE t (value INTEGER, id INTEGER)");
    second.execute("INSERT INTO t VALUES (20, 10)");
    auto second_rows = second.execute<id_value_row_t>("SELECT * FROM t");
    ASSERT_EQ(1, second_rows.size());
    EXPECT_EQ(10, second_rows[0].id);
    EXPECT_EQ(20, second_rows[0].value);
}

TEST(Sqlite3ppTest, StructColumnMapInvalidatedBySchemaChange) {
    // Same connection, same SQL text, new column layout: SQLite re-prepares the
    // cached statement, so the cached field indices must be rebuilt.
    Sqlite3pp db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER, value INTEGER)");
    db.execute("INSERT INTO t VALUES (1, 2)");
    auto before = db.execute<id_value_row_t>("SELECT * FROM t");
    ASSERT_EQ(1, before.size());
    EXPECT_EQ(1, before[0].id);

    db.execute("ALTER TABLE t RENAME TO t_old");
    db.execute("CREATE TABLE t (value INTEGER, id INTEGER)");
    db.execute("INSERT INTO t SELECT value, id FROM t_old");

    auto after = db.execute<id_value_row_t>("SELECT * FROM t");
    ASSERT_EQ(1, after.size());
    EXPECT_EQ(1, after[0].id);
    EXPECT_EQ(2, after[0].value);
}

TEST(Sqlite3ppTest, NonOptionalNullThrows) {
    Sqlite3pp db(":memory:");
    db.execute("CREATE TABLE t (value INTEGER)");
    db.execute("INSERT INTO t VALUES (NULL)");

    EXPECT_THROW(db.execute<required_row_t>("SELECT value FROM t"), std::runtime_error);
}

TEST(Sqlite3ppTest, MissingOptionalColumnMapsToNullopt) {
    Sqlite3pp db(":memory:");

    auto rows = db.execute<optional_missing_row_t>("SELECT 7 AS id");
    ASSERT_EQ(1, rows.size());
    EXPECT_EQ(7, rows[0].id);
    EXPECT_FALSE(rows[0].name.has_value());
}

TEST(Sqlite3ppTest, MissingRequiredColumnThrows) {
    Sqlite3pp db(":memory:");

    EXPECT_THROW(db.execute<required_missing_row_t>("SELECT 7 AS value"), std::runtime_error);
}

TEST(Sqlite3ppTest, CachedStatement) {
    Sqlite3pp db(":memory:");

    db.execute(R"(
        CREATE TABLE IF NOT EXISTS c_user (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            age INTEGER
        )
    )");

    db.executeOnce("INSERT INTO c_user (name, age) VALUES (?, ?)", "Alice", 30);
    db.executeOnce("INSERT INTO c_user (name, age) VALUES (?, ?)", "Bob", 25);

    // First call prepares and caches; second call reuses
    auto r1 = db.execute<cnt_result_t>("SELECT count(*) AS cnt FROM c_user");
    EXPECT_EQ(2, r1[0].cnt);

    auto r2 = db.execute<cnt_result_t>("SELECT count(*) AS cnt FROM c_user");
    EXPECT_EQ(2, r2[0].cnt);
}

TEST(Sqlite3ppTest, CountResult) {
    Sqlite3pp db(":memory:");

    db.execute("CREATE TABLE IF NOT EXISTS t (x INTEGER)");
    db.execute("INSERT INTO t VALUES (1)");
    db.execute("INSERT INTO t VALUES (2)");

    auto rows = db.execute<cnt_result_t>("SELECT COUNT(*) AS cnt FROM t");
    ASSERT_EQ(1, rows.size());
    EXPECT_EQ(2, rows[0].cnt);
}

// NOTE: beginTransaction() / endTransaction() have an edge case with :memory:
// databases in WAL mode. Transactions via raw SQL work correctly.
TEST(Sqlite3ppTest, TransactionViaRawSQL) {
    Sqlite3pp db(":memory:");
    db.execute("CREATE TABLE t (x INTEGER)");

    db.executeOnce("BEGIN");
    db.executeOnce("INSERT INTO t VALUES (42)");
    db.executeOnce("COMMIT");

    auto rows = db.execute<cnt_result_t>("SELECT COUNT(*) AS cnt FROM t");
    EXPECT_EQ(1, rows[0].cnt);
}

TEST(Sqlite3ppTest, RollbackViaRawSQL) {
    Sqlite3pp db(":memory:");
    db.execute("CREATE TABLE t (x INTEGER)");

    db.executeOnce("BEGIN");
    db.executeOnce("INSERT INTO t VALUES (99)");
    db.executeOnce("ROLLBACK");

    auto rows = db.execute<cnt_result_t>("SELECT COUNT(*) AS cnt FROM t");
    EXPECT_EQ(0, rows[0].cnt);
}

TEST(Sqlite3ppTest, NestedBeginTransactionThrows) {
    // Regression: a nested beginTransaction() ended the outer transaction first,
    // silently COMMITTING it and dropping the outer scope's atomicity.
    namespace fs = std::filesystem;
    auto path = fs::temp_directory_path() / "cc_s3pp_nested_tx.sqlite3";
    fs::remove(path);

    Sqlite3pp db(path.string());
    db.execute("CREATE TABLE t (v INTEGER)");

    db.beginTransaction();
    EXPECT_THROW(db.beginTransaction(), std::runtime_error);
    db.executeOnce("INSERT INTO t VALUES (1)");
    db.endTransaction(true);  // roll back the outer transaction

    auto rows = db.execute<cnt_result_t>("SELECT COUNT(*) AS cnt FROM t");
    EXPECT_EQ(0, rows[0].cnt);

    db.close();
    fs::remove(path);
}

TEST(Sqlite3ppTest, MultipleSessionsSameThreadDistinctConnections) {
    // Regression: conn_ used to be a single thread_local shared by ALL Session
    // instances — the second dbpath was silently ignored.
    namespace fs = std::filesystem;
    auto p1 = fs::temp_directory_path() / "cc_s3pp_test_a.sqlite3";
    auto p2 = fs::temp_directory_path() / "cc_s3pp_test_b.sqlite3";
    std::filesystem::remove(p1);
    std::filesystem::remove(p2);

    cc::Sqlite3Session a(p1.string());
    cc::Sqlite3Session b(p2.string());
    a->execute("CREATE TABLE t (v INTEGER)");
    b->execute("CREATE TABLE t (v INTEGER)");
    a->execute("INSERT INTO t VALUES (1)");
    b->execute("INSERT INTO t VALUES (2)");

    auto ra = a->execute<std::tuple<int>>("SELECT v FROM t");
    auto rb = b->execute<std::tuple<int>>("SELECT v FROM t");
    EXPECT_EQ(1, std::get<0>(ra.at(0)));
    EXPECT_EQ(2, std::get<0>(rb.at(0)));

    std::filesystem::remove(p1);
    std::filesystem::remove(p2);
}
