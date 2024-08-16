#pragma once

#ifndef CC_WITH_SQLITE3
#    error "Recompile with CC_WITH_SQLITE3"
#endif

#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <boost/core/noncopyable.hpp>
#include <boost/hana.hpp>
#include <cc/type_traits.h>
#include <cc/util.h>
#include <fmt/format.h>
#include <sqlite3.h>
#include <stdint.h>

namespace cc {

namespace hana = boost::hana;

namespace detail {

template <typename T>
constexpr bool is_array_v =
    cc::is_vector_v<T> || cc::is_list_v<T> || cc::is_set_v<T> || cc::is_unordered_set_v<T>;

template <typename T>
constexpr bool is_object_v = cc::is_map_v<T> || cc::is_unordered_map_v<T>;

template <typename T>
struct sqlite3_type {
    static std::string get() {
        if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>
                      || std::is_same_v<T, const char*>) {
            return "TEXT";
        } else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
            return "REAL";
        } else if constexpr (std::is_integral_v<T>) {
            return "INTEGER";
        } else if constexpr (std::is_same_v<T, std::vector<char>>) {
            return "BLOB";
        } else if constexpr (is_array_v<T> || is_object_v<T> || hana::Struct<T>::value) {
            return "JSON";
        } else {
            return "TEXT";
        }
    }
};

template <typename T>
struct sqlite3_type<std::optional<T>> {
    static std::string get() { return sqlite3_type<T>::get(); }
};

}  // namespace detail

template <typename MutexPolicy = cc::NonMutex, template <typename> class ReadLock = cc::LockGuard,
          template <typename> class WriteLock = cc::LockGuard>
class Sqlite3pp final : boost::noncopyable {
public:
    Sqlite3pp() : closed_(false), auto_commit_(true), conn_(nullptr) {}

    ~Sqlite3pp() { close(); }

    void open(std::string_view sourcename, int timeout = -1) {
        WriteLock<MutexPolicy> _lck(mtx_);
        if (conn_) {
            throw std::runtime_error("sqlite3 already opened !!!");
        }

        int mode = SQLITE_OPEN_READWRITE;
        if (sourcename == ":memory:") {
            mode |= SQLITE_OPEN_MEMORY;
        } else {
            mode |= SQLITE_OPEN_CREATE;
        }
        int res = sqlite3_open_v2(sourcename.data(), &conn_, mode, nullptr);
        if (res != SQLITE_OK) {
            auto errmsg = sqlite3_errmsg(conn_);
            sqlite3_close_v2(conn_);
            conn_ = nullptr;
            throw std::runtime_error(std::string("sqlite3_open_v2:") + errmsg);
        }

        if (timeout > 0) {
            sqlite3_busy_timeout(conn_, timeout);
        }

        // pragma
        execute("PRAGMA journal_mode=WAL;");
        execute("PRAGMA synchronous=NORMAL;");
        execute("PRAGMA case_sensitive_like = ON;");
        execute("PRAGMA locking_mode = EXCLUSIVE;");

        closed_ = false;
    }

    void close() {
        WriteLock<MutexPolicy> _lck(mtx_);
        if (!closed_) {
            closed_ = true;
            if (conn_) {
                sqlite3_close_v2(conn_);
                conn_ = nullptr;
            }
        }
    }

    template <typename R = void, typename... Args>
    std::enable_if_t<!std::is_void_v<R>, std::vector<R>>
    execute(std::string_view stmt, Args&&... args) {
        WriteLock<MutexPolicy> _lck(mtx_);
        return execute_impl<R>(stmt, std::forward<Args>(args)...);
    }

    template <typename R = void, typename... Args>
    std::enable_if_t<std::is_void_v<R>, void> execute(std::string_view stmt, Args&&... args) {
        WriteLock<MutexPolicy> _lck(mtx_);
        execute_impl<void>(stmt, std::forward<Args>(args)...);
    }

    // fake orm
    template <typename T, typename = hana::when<hana::Struct<T>::value>>
    void
    create_table(std::string_view tbl_name, std::vector<std::vector<std::string>> indexes = {}) {
        std::vector<std::string> stmts;
        std::string stmt;
        std::string seq;
        std::vector<std::string> parts;
        bool create_at = false;
        bool update_at = false;

        parts.emplace_back();

        stmt = fmt::format("CREATE TABLE IF NOT EXISTS {} (", tbl_name);
        stmt += seq;
        stmt += "\n  id INTEGER PRIMARY KEY AUTOINCREMENT";
        seq = ",";

        hana::for_each(T{}, [&](const auto& pair) {
            auto key           = hana::to<char const*>(hana::first(pair));
            const auto& member = hana::second(pair);
            using Member       = std::remove_const_t<std::remove_reference_t<decltype(member)>>;

            std::string type     = detail::sqlite3_type<Member>::get();
            std::string not_null = cc::is_optional_v<Member> ? "" : " NOT NULL";

            stmt += seq;
            stmt += fmt::format("\n  {} {}{}", key, type, not_null);

            if (std::string_view(key) == "created_at") {
                create_at = true;
            }
            if (std::string_view(key) == "updated_at") {
                update_at = true;
            }
        });

        if (!create_at) {
            stmt += seq;
            stmt += "\n  created_at TIMESTAMP DEFAULT (unixepoch('now'))";
        }

        if (!update_at) {
            stmt += seq;
            stmt += "\n  updated_at TIMESTAMP DEFAULT (unixepoch('now'))";
        }

        stmt += "\n);";
        stmts.emplace_back((std::string&&)stmt);

        // auto update
        stmt = fmt::format(R"(CREATE TRIGGER IF NOT EXISTS {}_updated_at_trigger AFTER UPDATE ON {}
BEGIN
  UPDATE user SET updated_at = unixepoch('now') WHERE id == NEW.id;
END;)",
                           tbl_name, tbl_name);
        stmts.emplace_back((std::string&&)stmt);

        // index
        for (const auto& idx : indexes) {
            stmt = fmt::format("CREATE INDEX IF NOT EXISTS {}_{}_idx ON {}({});", tbl_name,
                               fmt::join(idx, "_"), tbl_name, fmt::join(idx, ","));
            stmts.emplace_back((std::string&&)stmt);
        }

        for (auto& s : stmts) {
            execute(s);
        }
    }

    template <typename T, typename = hana::when<hana::Struct<T>::value>>
    void create_table(std::string_view tbl, std::string_view column) {
        create_table<T>(tbl, {{std::string(column)}});
    }

private:
    template <typename T>
    static void sqlite3pp_bind(sqlite3_stmt* vm, int param_no, T&& t) {
        using T0 = std::decay_t<T>;
        int rc   = 0;

        if constexpr (std::is_integral_v<T0>) {
            if constexpr (sizeof(T0) <= 4) {
                rc = sqlite3_bind_int(vm, param_no, static_cast<int>(std::forward<T>(t)));
            } else {
                rc = sqlite3_bind_int64(vm, param_no, static_cast<int64_t>(std::forward<T>(t)));
            }
        } else if constexpr (std::is_same_v<T0, float> || std::is_same_v<T0, double>) {
            rc = sqlite3_bind_double(vm, param_no, std::forward<T>(t));
        } else if constexpr (std::is_same_v<T0, const char*>) {
            rc = sqlite3_bind_text(vm, param_no, t, -1, SQLITE_TRANSIENT);
        } else if constexpr (std::is_same_v<T0, std::string>
                             || std::is_same_v<T0, std::string_view>) {
            // rc                 = sqlite3_bind_null(vm, param_no);
            rc = sqlite3_bind_text(vm, param_no, t.data(), t.size(), SQLITE_TRANSIENT);
        } else if constexpr (std::is_same_v<T0, std::vector<char>>) {
            rc = sqlite3_bind_text(vm, param_no, t.data(), t.size(), SQLITE_TRANSIENT);
        } else {
            throw std::runtime_error("Unknown sqlite3 type !!!");
        }

        if (rc != SQLITE_OK) {
            throw std::runtime_error(std::string("sqlite3_bind err:") + std::to_string(rc));
        }
    }

    template <typename T>
    static void sqlite3pp_column(sqlite3_stmt* vm, int column, T& t) {
        if constexpr (std::is_integral_v<T>) {
            if constexpr (sizeof(t) <= 4) {
                t = sqlite3_column_int(vm, column);
            } else {
                t = sqlite3_column_int64(vm, column);
            }
        } else if constexpr (std::is_same_v<double, T> || std::is_same_v<T, float>) {
            t = sqlite3_column_double(vm, column);
        } else if constexpr (std::is_same_v<T, std::string>) {
            auto s  = (const char*)sqlite3_column_text(vm, column);
            int len = sqlite3_column_bytes(vm, column);
            t       = std::string(s, len);
        } else if constexpr (std::is_same_v<T, std::vector<char>>) {
            auto blob = (const char*)sqlite3_column_blob(vm, column);
            int len   = sqlite3_column_bytes(vm, column);
            t         = std::vector<char>(blob, blob + len);
        } else {
            throw std::runtime_error("Unknown sqlite3 type !!!");
        }
    }

    /*
  ** Execute an SQL statement.
  ** Return a Cursor object if the statement is a query, otherwise
  ** return the number of tuples affected by the statement.
  */
    template <typename R, typename... Args>
    std::conditional_t<std::is_void_v<R>, void, std::vector<R>>
    execute_impl(std::string_view stmt, Args&&... args) {
        int res;
        sqlite3_stmt* vm;
        const char* tail;

        check_conn();

        res = sqlite3_prepare_v3(conn_, stmt.data(), -1, 0, &vm, &tail);
        if (res != SQLITE_OK) {
            throw_except("sqlite3_prepare_v3");
        }

        if constexpr (sizeof...(args) > 0) {
            int index = 1;
            ((sqlite3pp_bind(vm, index++, std::forward<Args>(args))), ...);
        }

        // 执行SQL语句并遍历结果集
        if constexpr (is_tuple_v<R>) {
            std::vector<R> ret;
            while ((res = sqlite3_step(vm)) == SQLITE_ROW) {
                R e;
                auto f = [&](auto&... e0) {
                    int index = 0;
                    ((sqlite3pp_column<std::decay_t<decltype(e0)>>(vm, index++, e0)), ...);
                };
                std::apply(f, e);
                ret.emplace_back((R&&)e);
            }

            if (res != SQLITE_DONE) {
                throw_except("execute");
            }

            sqlite3_finalize(vm);
            return ret;
        } else if constexpr (hana::Struct<R>::value) {
            std::vector<R> ret;
            std::unordered_map<std::string, int> colname;
            while ((res = sqlite3_step(vm)) == SQLITE_ROW) {
                if (colname.empty()) {
                    int count = sqlite3_column_count(vm);
                    for (int i = 0; i < count; i++) {
                        colname.emplace(sqlite3_column_name(vm, i), i);
                    }
                }

                R e;
                hana::for_each(hana::keys(e), [&](const auto& key) {
                    auto keyname = hana::to<char const*>(key);
                    auto& member = hana::at_key(e, key);
                    using Member = std::decay_t<decltype(member)>;
                    int idx      = colname.at(keyname);
                    sqlite3pp_column<Member>(vm, idx, member);
                });
                ret.emplace_back((R&&)e);
            }

            if (res != SQLITE_DONE) {
                throw_except("execute");
            }

            sqlite3_finalize(vm);
            return ret;
        } else if constexpr (std::is_same_v<R, void>) {
            while ((res = sqlite3_step(vm)) == SQLITE_ROW) {
            }

            if (res != SQLITE_DONE) {
                throw_except("execute");
            }

            sqlite3_finalize(vm);
        } else {
            throw std::runtime_error("R type error !!!");
        }
    }

    inline void check_conn() {
        // ReadLock<MutexPolicy> _lck(mtx_);
        if (!conn_) {
            throw std::runtime_error("Expect connection !!!");
        }
    }

    inline void throw_except(std::string_view what = "") {
        if (!conn_) {
            return;
        }

        std::string msg;
        if (what.empty()) {
            msg = sqlite3_errmsg(conn_);
        } else {
            msg = std::string(what) + ": " + sqlite3_errmsg(conn_);
        }
        throw std::runtime_error(msg);
    }

private:
    MutexPolicy mtx_;
    bool closed_;
    bool auto_commit_;
    sqlite3* conn_;
};

}  // namespace cc
