#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <boost/container/flat_map.hpp>
#include <boost/noncopyable.hpp>
#include <cc/lru_cache.h>
#include <cc/reflection.h>
#include <cc/type_traits.h>
#include <gsl/gsl>
#include <sqlite3.h>

namespace cc {

namespace detail {
// Atomic constraints short-circuit: a T without value_type (e.g. char*, int*) is
// false here instead of a hard error, so it falls through to the "Unknown
// sqlite3 type" branch in bindParam().
template <typename T>
concept char_text = requires { typename T::value_type; } && std::is_same_v<typename T::value_type, char>;

inline void mk_parent_dir(const std::string& path) {
    std::filesystem::path fp(path);
    auto parent_path = fp.parent_path();
    if (!parent_path.empty()) {
        std::filesystem::create_directories(parent_path);
    }
}
}  // namespace detail

// Constrain row types to reflectable structs (not std::string, std::vector, etc.)
template <typename T>
concept row_struct = reflection::field_countable<T> && !cc::is_tuple_v<T>;

// Thread-confined connections: Sqlite3pp is NOT safe to share across threads
// (statement cache and pragma state are unlocked). Use Sqlite3Session for a
// per-(thread, dbpath) pool.
class Sqlite3pp : boost::noncopyable {
    class StmtDeleter {
    public:
        inline void operator()(sqlite3_stmt* vm) const noexcept {
            if (vm) sqlite3_finalize(vm);
        }
    };

    typedef std::unique_ptr<sqlite3_stmt, StmtDeleter> stmt_type;
    typedef LRUCache<std::string, stmt_type> cache_type;

    const std::string dbpath_;
    const int timeout_;
    sqlite3* db_ = nullptr;
    cache_type cache_;
    // SQLite bumps this counter whenever it silently re-prepares a statement
    // (schema change), which is the invalidation signal for fi_cache_.
    static unsigned reprepareCount(sqlite3_stmt* vm) {
        return static_cast<unsigned>(sqlite3_stmt_status(vm, SQLITE_STMTSTATUS_REPREPARE, 0));
    }

    // Column indices of the last struct SELECT per SQL text, plus the statement's
    // auto-reprepare counter at build time (SQLite bumps it on a schema change).
    boost::container::flat_map<std::string, std::pair<unsigned, std::vector<int>>> fi_cache_;
    bool in_transaction_;

public:
    Sqlite3pp(std::string_view dbpath, int timeout = -1)
        : dbpath_(std::string(dbpath)),
          timeout_(timeout),
          db_(open(dbpath_, timeout_)),
          cache_(128, 3600),
          in_transaction_(false) {
        sqlite3_initialize();
        execute("PRAGMA journal_mode=WAL;");
        executeOnce("PRAGMA synchronous=NORMAL;");
        executeOnce("PRAGMA locking_mode=NORMAL;");
        // executeOnce("PRAGMA case_sensitive_like=ON;");
    }

    ~Sqlite3pp() noexcept { close(); }

    void flush() {
        using row_type = std::tuple<std::string>;
        auto r = execute<row_type>("PRAGMA journal_mode");
        auto mode = std::get<0>(r.at(0));
        execute("PRAGMA journal_mode=DELETE;");
        // Restore original journal mode (comes from SQLite, safe for concatenation)
        execute("PRAGMA journal_mode=" + mode + ";");
    }

    void close() noexcept {
        if (db_) {
            try {
                flush();
            } catch (...) {
                // Best-effort: ignore flush errors during close
            }
            sqlite3_close_v2(db_);
            db_ = nullptr;
        }
    }

    void beginTransaction() {
        // Nesting used to endTransaction() first, silently COMMITTING the outer
        // transaction and dropping its atomicity. Refuse instead of guessing.
        if (in_transaction_) [[unlikely]] {
            throw std::runtime_error("beginTransaction: already in a transaction");
        }
        execute("BEGIN");
        in_transaction_ = true;
    }

    void endTransaction(bool failed = false) {
        if (in_transaction_) [[likely]] {
            if (failed) [[unlikely]] {
                execute("ROLLBACK");
            } else {
                execute("COMMIT");
            }
        }
        in_transaction_ = false;
    }

    template <typename Result = void, typename... Args>
    std::conditional_t<std::is_void_v<Result>, void, std::vector<Result>>
    execute(const std::string& stmt, Args&&... args) {
        return executeImpl<1, Result, Args...>(stmt, std::forward<Args>(args)...);
    }

    template <typename Result = void, typename... Args>
    std::conditional_t<std::is_void_v<Result>, void, std::vector<Result>>
    executeOnce(const std::string& stmt, Args&&... args) {
        return executeImpl<0, Result, Args...>(stmt, std::forward<Args>(args)...);
    }

private:
    static sqlite3* open(const std::string& dbpath, int timeout = -1) {
        sqlite3* conn;

        int mode = SQLITE_OPEN_READWRITE;
        if (dbpath == ":memory:") [[unlikely]] {
            mode |= SQLITE_OPEN_MEMORY;
        } else {
            mode |= SQLITE_OPEN_CREATE;
            detail::mk_parent_dir(dbpath);
        }

        int res = sqlite3_open_v2(dbpath.c_str(), &conn, mode, nullptr);
        if (res != SQLITE_OK) [[unlikely]] {
            auto errmsg = sqlite3_errmsg(conn);
            sqlite3_close_v2(conn);
            throw std::runtime_error(std::string("sqlite3_open_v2:") + errmsg);
        }

        if (timeout > 0) {
            sqlite3_busy_timeout(conn, timeout);
        }

        return conn;
    }

    template <typename T0, typename T = std::decay_t<T0>>
    static int bindParam(sqlite3_stmt* vm, int param_no, T0&& t) {
        int rc;

        if constexpr (std::is_integral_v<T>) {
            // sqlite3_bind_int takes an int, so a 4-byte UNSIGNED value (uint32_t)
            // above INT_MAX would silently wrap negative. Only bind as int when
            // every value of T is representable.
            if constexpr (sizeof(T) < 4 || (sizeof(T) == 4 && std::is_signed_v<T>)) {
                rc = sqlite3_bind_int(vm, param_no, (int)std::forward<T0>(t));
            } else {
                rc = sqlite3_bind_int64(vm, param_no, (int64_t)std::forward<T0>(t));
            }
        } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
            rc = sqlite3_bind_double(vm, param_no, (double)std::forward<T0>(t));
        } else if constexpr (std::is_same_v<T, const char*> || std::is_same_v<T, char*>) {
            rc = sqlite3_bind_text(vm, param_no, t, -1, SQLITE_STATIC);
        } else if constexpr (std::is_same_v<T, std::vector<char>>) {
            // Must precede char_text, which also matches vector<char>: the read
            // path decodes vector<char> as BLOB, so bind it as one. bind_blob
            // maps a null data pointer to SQL NULL, hence zeroblob for the empty
            // case (an empty vector is an empty BLOB, not NULL).
            if (t.empty()) {
                rc = sqlite3_bind_zeroblob(vm, param_no, 0);
            } else {
                rc = sqlite3_bind_blob(vm, param_no, t.data(), (int)t.size(), SQLITE_STATIC);
            }
        } else if constexpr (detail::char_text<T>) {
            rc = sqlite3_bind_text(vm, param_no, t.data(), t.size(), SQLITE_STATIC);
        } else if constexpr (cc::is_optional_v<T>) {
            if (!t.has_value()) {
                rc = sqlite3_bind_null(vm, param_no);
            } else {
                rc = bindParam(vm, param_no, t.value());
            }
        } else {
            throw std::runtime_error("Unknown sqlite3 type !!!");
        }

        if (rc != SQLITE_OK) [[unlikely]] {
            throw std::runtime_error(std::string("sqlite3_bind err:") + std::to_string(rc));
        }
        return rc;
    }

    template <typename T>
    static void readColumn(sqlite3_stmt* vm, int col, T& t) {
        if constexpr (!cc::is_optional_v<T>) {
            // sqlite3_column_* return 0/NULL silently on NULL and never signal
            // missing columns; reading them would also be UB for text/blob
            // (nullptr -> std::string(nullptr, 0)). Reject explicitly.
            if (sqlite3_column_type(vm, col) == SQLITE_NULL) [[unlikely]] {
                throw std::runtime_error("NULL value for non-optional column");
            }
        }
        if constexpr (std::is_integral_v<T>) {
            if constexpr (sizeof(t) <= 4) {
                t = sqlite3_column_int(vm, col);
            } else {
                t = sqlite3_column_int64(vm, col);
            }
        } else if constexpr (std::is_same_v<double, T> || std::is_same_v<T, float>) {
            t = sqlite3_column_double(vm, col);
        } else if constexpr (std::is_same_v<T, std::string>) {
            auto s = (const char*)sqlite3_column_text(vm, col);
            int len = sqlite3_column_bytes(vm, col);
            t = std::string(s, len);
        } else if constexpr (std::is_same_v<T, std::vector<char>>) {
            int len = sqlite3_column_bytes(vm, col);
            if (len == 0) {
                t.clear();
            } else {
                auto blob = (const char*)sqlite3_column_blob(vm, col);
                t = std::vector<char>(blob, blob + len);
            }
        } else if constexpr (cc::is_optional_v<T>) {
            if (sqlite3_column_type(vm, col) == SQLITE_NULL) {
                t = std::nullopt;
            } else {
                using T0 = typename T::value_type;
                T0 t0;
                readColumn(vm, col, t0);
                t.emplace(std::move(t0));
            }
        } else {
            throw std::runtime_error("Unknown sqlite3 type !!!");
        }
    }

    template <bool Cached, typename... Args>
    sqlite3_stmt* buildStmt(const std::string& stmt, Args&&... args) {
        auto c = db_;
        sqlite3_stmt* vm;
        int rc;
        const char* tail;

        if constexpr (Cached) {
            auto vm_cached = cache_.getPtr(stmt);
            if (vm_cached != nullptr) {
                vm = vm_cached->get();
                sqlite3_reset(vm);
                sqlite3_clear_bindings(vm);
            } else {
                rc = sqlite3_prepare_v3(c, stmt.data(), -1, 0, &vm, &tail);
                if (rc != SQLITE_OK) [[unlikely]] {
                    throw std::runtime_error(std::string("sqlite3_prepare_v3:") + sqlite3_errmsg(c));
                }
                stmt_type tmp;
                tmp.reset(vm);
                cache_.set(stmt, (stmt_type&&)tmp);
            }
        } else {
            rc = sqlite3_prepare_v3(c, stmt.data(), -1, 0, &vm, &tail);
            if (rc != SQLITE_OK) [[unlikely]] {
                throw std::runtime_error(std::string("sqlite3_prepare_v3:") + sqlite3_errmsg(c));
            }
        }

        if constexpr (sizeof...(args) > 0) {
            int idx = 1;
            ((bindParam(vm, idx++, std::forward<Args>(args))), ...);
        }
        return vm;
    }

    template <bool Cached, typename Result = void, typename... Args>
    std::conditional_t<std::is_void_v<Result>, void, std::vector<Result>>
    executeImpl(const std::string& stmt, Args&&... args) {
        auto conn = db_;
        int rc;

        for (int attempt = 0; attempt < 2; ++attempt) {
            auto vm = buildStmt<Cached>(stmt, std::forward<Args>(args)...);
            auto defer = gsl::finally([&] {
                if constexpr (!Cached) sqlite3_finalize(vm);
            });

            if constexpr (std::is_void_v<Result>) {
                while ((rc = sqlite3_step(vm)) == SQLITE_ROW) {
                }
                if (rc == SQLITE_DONE) return;
                if (rc == SQLITE_SCHEMA) {
                    if constexpr (Cached) cache_.remove(stmt);
                    continue;
                }
                throw std::runtime_error(std::string("Execute error:") + sqlite3_errmsg(conn));
            } else {
                std::vector<Result> ret;
                if constexpr (cc::is_tuple_v<Result>) {
                    while ((rc = sqlite3_step(vm)) == SQLITE_ROW) {
                        Result e;
                        std::apply(
                            [&](auto&... e0) {
                                int idx = 0;
                                ((readColumn<std::decay_t<decltype(e0)>>(vm, idx++, e0)), ...);
                            },
                            e);
                        ret.emplace_back(std::move(e));
                    }
                } else if constexpr (row_struct<Result>) {
                    // Same SQL text + same Result type => same column layout, until SQLite
                    // silently auto-reprepares the statement after a schema change. That
                    // bumps the statement's reprepare counter, so a counter mismatch means
                    // the cached indices are stale. Checked per row: a re-prepare can land
                    // inside sqlite3_step.
                    std::vector<int> local_indices;
                    auto cached = fi_cache_.end();
                    bool have_cache = false;
                    if constexpr (Cached) {
                        cached = fi_cache_.find(stmt);
                        have_cache = (cached != fi_cache_.end() && cached->second.first == reprepareCount(vm));
                    }

                    while ((rc = sqlite3_step(vm)) == SQLITE_ROW) {
                        if (!have_cache || (Cached && cached->second.first != reprepareCount(vm))) {
                            boost::container::flat_map<std::string_view, int, std::less<>> colmap;
                            int count = sqlite3_column_count(vm);
                            for (int i = 0; i < count; i++) {
                                colmap.emplace(sqlite3_column_name(vm, i), i);
                            }
                            local_indices.clear();
                            Result tmp;
                            reflection::for_each_field(tmp, [&](std::string_view k, auto&) {
                                auto it = colmap.find(k);
                                local_indices.push_back(it != colmap.end() ? it->second : -1);
                            });
                            have_cache = true;
                            if constexpr (Cached) {
                                const auto reprepare = reprepareCount(vm);
                                cached =
                                    fi_cache_
                                        .insert_or_assign(stmt, std::make_pair(reprepare, std::vector<int>{}))
                                        .first;
                                cached->second.second.swap(local_indices);
                            }
                        }

                        const auto& field_indices = (Cached && have_cache) ? cached->second.second
                                                                           : local_indices;
                        Result e;
                        int fi = 0;
                        reflection::for_each_field(e, [&](std::string_view name, auto& v) {
                            using Member = std::decay_t<decltype(v)>;
                            int idx = field_indices[fi++];
                            if (idx < 0) {
                                if constexpr (cc::is_optional_v<Member>) {
                                    v = std::nullopt;
                                } else {
                                    throw std::runtime_error("Missing required column: " + std::string(name));
                                }
                            } else {
                                readColumn<Member>(vm, idx, v);
                            }
                        });
                        ret.emplace_back((Result&&)e);
                    }
                } else {
                    throw std::runtime_error("Unknown return type");
                }

                if (rc == SQLITE_SCHEMA) {
                    if constexpr (Cached) cache_.remove(stmt);
                    continue;
                }

                if (rc != SQLITE_DONE) [[unlikely]] {
                    throw std::runtime_error(std::string("Execute error:") + sqlite3_errmsg(conn));
                }

                return ret;
            }
        }
        throw std::runtime_error(std::string("Execute error (schema change):") + sqlite3_errmsg(conn));
    }
};

// One Sqlite3pp per (thread, dbpath): a thread_local pool keyed by dbpath, so
// multiple Session instances with different paths each get their own
// connection (a shared single connection would silently reuse the first path).
// Per-thread timeouts follow the first Session that opens a given dbpath.
//
// Connections are only released at thread exit — NOT suited for short-lived
// use. For a short-lived connection, construct a local Sqlite3pp instead; its
// destructor flushes and closes immediately. Keep SQLite's threading mode under
// the process owner's control; sqlite3_config() is process-wide and may fail
// after SQLite has been initialized.
class Sqlite3Session : boost::noncopyable {
    const std::string dbpath_;
    const int timeout_;
    static inline thread_local std::unordered_map<std::string, std::unique_ptr<Sqlite3pp>> conns_;

public:
    Sqlite3Session(std::string_view dbpath, int timeout = -1) : dbpath_(dbpath), timeout_(timeout) {
        getConn();
    }

    Sqlite3pp* operator->() { return getConn(); }
    Sqlite3pp& operator*() { return *getConn(); }

private:
    Sqlite3pp* getConn() {
        auto& conn = conns_[dbpath_];
        if (!conn) [[unlikely]] {
            conn = std::make_unique<Sqlite3pp>(dbpath_, timeout_);
        }
        return conn.get();
    }
};

}  // namespace cc
