#pragma once

#include <cc/config.h>

#ifndef CC_WITH_SQLITE3
#    error "Recompile with CC_WITH_SQLITE3"
#endif

#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <boost/hana.hpp>
#include <cc/type_traits.h>
#include <cc/util.h>
#include <sqlite3.h>

namespace cc {

namespace hana = boost::hana;

template <typename MutexPolicy = cc::NonMutex, template <typename> class ReadLock = cc::LockGuard,
          template <typename> class WriteLock = cc::LockGuard>
class Sqlite3pp final {
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
    std::enable_if_t<std::is_void_v<R>, void>
    execute(std::string_view stmt, Args&&... args) {
        WriteLock<MutexPolicy> _lck(mtx_);
        execute_impl<void>(stmt, std::forward<Args>(args)...);
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
                rc = sqlite3_bind_int64(vm, param_no,
                                        static_cast<int64_t>(std::forward<T>(t)));
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
