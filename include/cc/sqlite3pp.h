#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <boost/noncopyable.hpp>
#include <cc/type_traits.h>
#include <field_reflection.hpp>  // cpp_yyjson
#include <gsl/gsl>
#include <sqlite3.h>

namespace cc {

namespace detail {
inline void mk_parent_dir(const std::string& path) {
    std::filesystem::path fp(path);
    std::filesystem::create_directories(fp.parent_path());
}
}  // namespace detail

class Sqlite3pp : boost::noncopyable {
    struct Sqlite3Deleter {
        inline void operator()(sqlite3* c) const noexcept {
            if (c) sqlite3_close_v2(c);
        }
    };

    template <typename T>
    struct is_text {
        static constexpr bool value = std::is_same_v<typename T::value_type, char>;
    };

    template <typename T>
    struct is_struct {
        static constexpr bool value = std::is_class_v<T> && !std::is_union_v<T>;
    };

    const std::string dbpath_;
    const int timeout_;
    static inline thread_local std::unique_ptr<sqlite3, Sqlite3Deleter> conn_ = nullptr;

public:
    Sqlite3pp(std::string dbpath, int timeout = -1) : dbpath_(dbpath), timeout_(timeout) {
        sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);
        sqlite3_initialize();
        auto c = get_conn();
        execute("PRAGMA journal_mode=WAL;");
        execute("PRAGMA synchronous=NORMAL;");
        execute("PRAGMA locking_mode=NORMAL;");
        execute("PRAGMA case_sensitive_like=ON;");
    }

    ~Sqlite3pp() {}

    void flush() {
        using row_t = std::tuple<std::string>;
        auto r      = execute<row_t>("PRAGMA journal_mode");
        auto mode   = std::get<0>(r.at(0));
        execute("PRAGMA journal_mode=DELETE;");
        execute("PRAGMA journal_mode=" + mode + ";");
    }

    template <typename R = void, typename... Args>
    std::enable_if_t<std::is_void_v<R>, void> execute(std::string_view stmt, Args&&... args) {
        auto conn = get_conn();
        auto vm   = build_stmt(stmt, std::forward<Args>(args)...);
        int rc;

        while ((rc = sqlite3_step(vm)) == SQLITE_ROW) {
        }

        if (GSL_UNLIKELY(rc != SQLITE_DONE)) {
            throw std::runtime_error(std::string("Execute error:") + sqlite3_errmsg(conn));
        }
        sqlite3_finalize(vm);
    }

    template <typename R = void, typename... Args>
    std::enable_if_t<!std::is_void_v<R>, std::vector<R>>
    execute(std::string_view stmt, Args&&... args) {
        auto conn = get_conn();
        auto vm   = build_stmt(stmt, std::forward<Args>(args)...);
        int rc;

        std::vector<R> ret;
        ret.reserve(16);
        if constexpr (cc::is_tuple_v<R>) {
            while ((rc = sqlite3_step(vm)) == SQLITE_ROW) {
                R e;
                std::apply(
                    [&](auto&... e0) {
                        int idx = 0;
                        ((fillcol<std::decay_t<decltype(e0)>>(vm, idx++, e0)), ...);
                    },
                    e);
                ret.emplace_back(std::move(e));
            }
        } else if constexpr (is_struct<R>::value) {
            std::unordered_map<std::string, int> colmap;
            while ((rc = sqlite3_step(vm)) == SQLITE_ROW) {
                if (colmap.empty()) {
                    int count = sqlite3_column_count(vm);
                    for (int i = 0; i < count; i++) {
                        colmap.emplace(sqlite3_column_name(vm, i), i);
                    }
                }

                R e;
                field_reflection::for_each_field(e, [&](std::string_view k, auto& v) {
                    using Member = std::decay_t<decltype(v)>;
                    int idx      = colmap.at(std::string(k));
                    fillcol<Member>(vm, idx, v);
                });
                ret.emplace_back((R&&)e);
            }
        } else {
            throw std::runtime_error("Unknown return type");
        }

        if (GSL_UNLIKELY(rc != SQLITE_DONE)) {
            throw std::runtime_error(std::string("Execute error:") + sqlite3_errmsg(conn));
        }

        sqlite3_finalize(vm);
        return ret;
    }

    int64_t last_insert_rowid() { return sqlite3_last_insert_rowid(get_conn()); }

    int affected_rows() { return sqlite3_changes(get_conn()); }

    void close() {
        if (conn_) {
            flush();
            sqlite3_close_v2(conn_.get());
            conn_.reset();
        }
    }

private:
    sqlite3* get_conn() {
        if (!conn_) {
            auto c = open(dbpath_, timeout_);
            conn_.reset(c);
        }

        return conn_.get();
    }

    static sqlite3* open(std::string dbpath, int timeout = -1) {
        sqlite3* conn;

        int mode = SQLITE_OPEN_READWRITE;
        if (GSL_UNLIKELY(dbpath == ":memory:")) {
            mode |= SQLITE_OPEN_MEMORY;
        } else {
            mode |= SQLITE_OPEN_CREATE;
            detail::mk_parent_dir(dbpath);
        }

        int res = sqlite3_open_v2(dbpath.c_str(), &conn, mode, nullptr);
        if (GSL_UNLIKELY(res != SQLITE_OK)) {
            auto errmsg = sqlite3_errmsg(conn);
            sqlite3_close_v2(conn);
            throw std::runtime_error(std::string("sqlite3_open_v2:") + errmsg);
            return nullptr;
        }

        if (timeout > 0) {
            sqlite3_busy_timeout(conn, timeout);
        }

        return conn;
    }

    template <typename T0, typename T = std::decay_t<T0>>
    static int bindone(sqlite3_stmt* vm, int param_no, T0&& t) {
        int rc;

        if constexpr (std::is_integral_v<T>) {
            if constexpr (sizeof(T) <= 4) {
                rc = sqlite3_bind_int(vm, param_no, (int)std::forward<T0>(t));
            } else {
                rc = sqlite3_bind_int64(vm, param_no, (int64_t)std::forward<T0>(t));
            }
        } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
            rc = sqlite3_bind_double(vm, param_no, (double)std::forward<T0>(t));
        } else if constexpr (std::is_same_v<T, const char*>) {
            rc = sqlite3_bind_text(vm, param_no, t, -1, SQLITE_TRANSIENT);
        } else if constexpr (is_text<T>::value) {
            rc = sqlite3_bind_text(vm, param_no, t.data(), t.size(), SQLITE_TRANSIENT);
        } else if constexpr (cc::is_optional_v<T>) {
            if (!t.has_value()) {
                rc = sqlite3_bind_null(vm, param_no);
            } else {
                using T1 = typename T::value_type;
                T1 t1    = t.value();
                rc       = bindone<T1>(vm, param_no, std::move(t1));
            }
        } else {
            throw std::runtime_error("Unknown sqlite3 type !!!");
        }

        if (rc != SQLITE_OK) {
            throw std::runtime_error(std::string("sqlite3_bind err:") + std::to_string(rc));
        }
        return rc;
    }

    template <typename T>
    static void fillcol(sqlite3_stmt* vm, int col, T& t) {
        if constexpr (std::is_integral_v<T>) {
            if constexpr (sizeof(t) <= 4) {
                t = sqlite3_column_int(vm, col);
            } else {
                t = sqlite3_column_int64(vm, col);
            }
        } else if constexpr (std::is_same_v<double, T> || std::is_same_v<T, float>) {
            t = sqlite3_column_double(vm, col);
        } else if constexpr (std::is_same_v<T, std::string>) {
            auto s  = (const char*)sqlite3_column_text(vm, col);
            int len = sqlite3_column_bytes(vm, col);
            t       = std::string(s, len);
        } else if constexpr (std::is_same_v<T, std::vector<char>>) {
            auto blob = (const char*)sqlite3_column_blob(vm, col);
            int len   = sqlite3_column_bytes(vm, col);
            t         = std::vector<char>(blob, blob + len);
        } else if constexpr (cc::is_optional_v<T>) {
            using T0 = typename T::value_type;
            T0 t0;
            fillcol(vm, col, t0);
            t = t0;
        } else {
            throw std::runtime_error("Unknown sqlite3 type !!!");
        }
    }

    template <typename... Args>
    sqlite3_stmt* build_stmt(std::string_view stmt, Args&&... args) {
        auto c = get_conn();
        sqlite3_stmt* vm;
        int rc;
        const char* tail;

        rc = sqlite3_prepare_v3(c, stmt.data(), -1, 0, &vm, &tail);
        if (GSL_UNLIKELY(rc != SQLITE_OK)) {
            throw std::runtime_error(std::string("sqlite3_prepare_v3:") + sqlite3_errmsg(c));
        }

        if constexpr (sizeof...(args) > 0) {
            int idx = 1;
            ((bindone(vm, idx++, std::forward<Args>(args))), ...);
        }
        return vm;
    }
};

}  // namespace cc
