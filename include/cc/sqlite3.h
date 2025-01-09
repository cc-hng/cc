#pragma once

#ifndef CC_WITH_SQLITE3
#    error "Recompile with CC_WITH_SQLITE3"
#endif

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <boost/core/noncopyable.hpp>
#include <cc/type_traits.h>
#include <cc/util.h>
#include <field_reflection.hpp>  // cpp_yyjson
#include <gsl/gsl>
#include <sqlite3.h>
#include <stdint.h>

namespace cc {

namespace detail {

template <typename T>
constexpr bool is_array_v =
    cc::is_vector_v<T> || cc::is_list_v<T> || cc::is_set_v<T> || cc::is_unordered_set_v<T>;

template <typename T>
constexpr bool is_object_v = cc::is_map_v<T> || cc::is_unordered_map_v<T>;

template <typename T>
constexpr bool is_struct_v = std::is_class_v<T> && !std::is_union_v<T>;

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
        } else if constexpr (is_array_v<T> || is_object_v<T>) {
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

inline void mk_parent_dir(std::string_view path) {
    std::filesystem::path fp(path.data());
    std::filesystem::create_directories(fp.parent_path());
}

}  // namespace detail

// clang-format off
template <
    typename MutexPolicy = NonMutex,
    template <class> class ReadLock = LockGuard,
    template <class> class WriteLock = LockGuard
>  // clang-format on
class Sqlite3pp final : boost::noncopyable {
public:
    Sqlite3pp() : closed_(false), auto_commit_(true), conn_(nullptr) {}

    ~Sqlite3pp() { close(); }

    void open(std::string_view sourcename, int timeout = -1) {
        WriteLock<MutexPolicy> _lck(mtx_);
        if (GSL_UNLIKELY(conn_)) {
            throw std::runtime_error("sqlite3 already opened !!!");
        }

        int mode = SQLITE_OPEN_READWRITE;
        if (GSL_UNLIKELY(sourcename == ":memory:")) {
            mode |= SQLITE_OPEN_MEMORY;
        } else {
            mode |= SQLITE_OPEN_CREATE;
            detail::mk_parent_dir(sourcename.data());
        }
        int res = sqlite3_open_v2(sourcename.data(), &conn_, mode, nullptr);
        if (GSL_UNLIKELY(res != SQLITE_OK)) {
            auto errmsg = sqlite3_errmsg(conn_);
            sqlite3_close_v2(conn_);
            conn_ = nullptr;
            throw std::runtime_error(std::string("sqlite3_open_v2:") + errmsg);
        }

        if (timeout > 0) {
            sqlite3_busy_timeout(conn_, timeout);
        }

        // pragma
        unsafe_execute("PRAGMA journal_mode=WAL;");
        unsafe_execute("PRAGMA synchronous=NORMAL;");
        unsafe_execute("PRAGMA case_sensitive_like=ON;");
        unsafe_execute("PRAGMA locking_mode=NORMAL;");

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

    decltype(auto) make_lock() { return std::make_unique<WriteLock<MutexPolicy>>(mtx_); }

    template <typename R = void, typename... Args>
    decltype(auto) execute(std::string_view stmt, Args&&... args) {
        WriteLock<MutexPolicy> _lck(mtx_);
        return unsafe_execute<R>(stmt, std::forward<Args>(args)...);
    }

    template <typename R = void, typename... Args>
    std::enable_if_t<!std::is_void_v<R>, std::vector<R>>
    unsafe_execute(std::string_view stmt, Args&&... args) {
        return execute_impl<R>(stmt, std::forward<Args>(args)...);
    }

    template <typename R = void, typename... Args>
    std::enable_if_t<std::is_void_v<R>> unsafe_execute(std::string_view stmt, Args&&... args) {
        return execute_impl<void>(stmt, std::forward<Args>(args)...);
    }

    int64_t last_insert_rowid() {
        WriteLock<MutexPolicy> _lck(mtx_);
        check_conn();
        return sqlite3_last_insert_rowid(conn_);
    }

    int affected_rows() {
        WriteLock<MutexPolicy> _lck(mtx_);
        check_conn();
        return sqlite3_changes(conn_);
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
        } else if constexpr (cc::is_optional_v<T0>) {
            if (!t.has_value()) {
                rc = sqlite3_bind_null(vm, param_no);
            } else {
                using T1 = typename T0::value_type;
                T1 t1    = t.value();
                sqlite3pp_bind<T1>(vm, param_no, std::move(t1));
            }
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
        } else if constexpr (cc::is_optional_v<T>) {
            using T0 = typename T::value_type;
            T0 t0;
            sqlite3pp_column(vm, column, t0);
            t = t0;
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
        if (GSL_UNLIKELY(res != SQLITE_OK)) {
            throw_except("sqlite3_prepare_v3");
        }

        if constexpr (sizeof...(args) > 0) {
            int index = 1;
            ((sqlite3pp_bind(vm, index++, std::forward<Args>(args))), ...);
        }

        // 执行SQL语句并遍历结果集
        if constexpr (is_tuple_v<R>) {
            std::vector<R> ret;
            ret.reserve(16);
            while ((res = sqlite3_step(vm)) == SQLITE_ROW) {
                R e;
                auto f = [&](auto&... e0) {
                    int index = 0;
                    ((sqlite3pp_column<std::decay_t<decltype(e0)>>(vm, index++, e0)), ...);
                };
                std::apply(f, e);
                ret.emplace_back((R&&)e);
            }

            if (GSL_UNLIKELY(res != SQLITE_DONE)) {
                throw_except("execute");
            }

            sqlite3_finalize(vm);
            return ret;
        } else if constexpr (detail::is_struct_v<R>) {
            std::vector<R> ret;
            ret.reserve(16);
            std::unordered_map<std::string, int> colname;
            while ((res = sqlite3_step(vm)) == SQLITE_ROW) {
                if (colname.empty()) {
                    int count = sqlite3_column_count(vm);
                    for (int i = 0; i < count; i++) {
                        colname.emplace(sqlite3_column_name(vm, i), i);
                    }
                }

                R e;
                field_reflection::for_each_field(e, [&](std::string_view k, auto& v) {
                    using Member = std::decay_t<decltype(v)>;
                    int idx      = colname.at(std::string(k));
                    sqlite3pp_column<Member>(vm, idx, v);
                });
                ret.emplace_back((R&&)e);
            }

            if (GSL_UNLIKELY(res != SQLITE_DONE)) {
                throw_except("execute");
            }

            sqlite3_finalize(vm);
            return ret;
        } else if constexpr (std::is_same_v<R, void>) {
            while ((res = sqlite3_step(vm)) == SQLITE_ROW) {
            }

            if (GSL_UNLIKELY(res != SQLITE_DONE)) {
                throw_except("execute");
            }

            sqlite3_finalize(vm);
        } else {
            throw std::runtime_error("R type error !!!");
        }
    }

    inline void check_conn() {
        // ReadLock<MutexPolicy> _lck(mtx_);
        if (GSL_UNLIKELY(!conn_)) {
            throw std::runtime_error("Expect connection !!!");
        }
    }

    inline void throw_except(std::string_view what = "") {
        if (GSL_UNLIKELY(!conn_)) {
            return;
        }

        std::string msg;
        if (GSL_UNLIKELY(what.empty())) {
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
