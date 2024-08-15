#pragma once

#include <functional>
#include <regex>
#include <tuple>
#include <vector>
#include <boost/core/noncopyable.hpp>
#include <cc/lit/object.h>

namespace cc {
namespace lit {

// template <typename Body = http::string_body>
class Router final : public boost::noncopyable {
public:
    using request_type  = http_request_t;
    using response_type = http_response_t;
    using kv_t          = http_request_t::kv_t;

public:
    Router() { handlers_.reserve(32); }

    ~Router() {}

    inline auto& use(http_handle_t&& h) {
        handlers_.emplace_back((http_handle_t&&)h);
        return *this;
    }

    auto& route(http::verb method, std::string_view path, http_handle_t&& handler) {
        class Functor {
        public:
            Functor(http::verb method, std::string_view path, http_handle_t&& handler)
              : method_(method)
              , parse_path_(compile_route(path))
              , handler_((http_handle_t&&)handler) {}

            boost::asio::awaitable<void>
            operator()(const request_type& req, response_type& resp, const http_next_handle_t& go) {
                if (method_ != (http::verb)-1) {
                    if (req->method() != method_) {
                        co_return co_await go();
                    }
                }

                auto [matched, params] = parse_path_(req.path);
                if (!matched) {
                    co_return co_await go();
                }

                req.params = params;
                co_await handler_(req, resp, go);
            }

        private:
            http::verb method_;
            std::function<std::tuple<bool, kv_t>(std::string_view)> parse_path_;
            http_handle_t handler_;
        };

        use(Functor(method, path, (http_handle_t&&)handler));
        return *this;
    }

    boost::asio::awaitable<void>
    run(const request_type& req, response_type& resp, const http_next_handle_t& go) {
        try {
            co_await run_impl(0, req, resp, go);
        } catch (std::exception& e) {
            resp->result(http::status::internal_server_error);
            resp->body() = e.what();
        }
    }

private:
    boost::asio::awaitable<void> run_impl(int idx, const request_type& req, response_type& resp,
                                          const http_next_handle_t& go = nullptr) {
        if (idx >= handlers_.size()) {
            if (go) {
                co_await go();
            }
            co_return;
        }

        const auto& fn = handlers_.at(idx);
        auto next      = [&, this, idx]() -> boost::asio::awaitable<void> {
            co_await run_impl(idx + 1, req, resp);
        };
        bool throw_exception = false;
        try {
            co_await fn(req, resp, next);
        } catch (std::exception& e) {
            throw_exception = true;
            resp->result(http::status::internal_server_error);
            resp->body() = e.what();
        }

        if (throw_exception) {
            co_await next();
        }
    }

public:
    /// @brief parse path to a kv map according to rule_t
    ///        throw a exception if parse failed.
    /// @param target:
    ///             "/user/:id/:name"
    ///             "/user/:id/:name?address=aa&phone=123456"
    ///         rule:
    /// @return {"id": "1", "name": "J", "address": "aa", "phone": "123456"}
    static std::function<std::tuple<bool, kv_t>(std::string_view)>
    compile_route(std::string_view path, bool whole = true) {
        class ParsePath {
        public:
            ParsePath() = default;

            void init(std::string_view path, bool whole) {
                std::string regex                     = "^" + std::string(path);
                std::string_view::size_type start_pos = 0;
                while (start_pos != std::string_view::npos) {
                    start_pos = regex.find(':', start_pos);
                    if (start_pos != std::string_view::npos) {
                        const auto next_end_pos  = regex.find(':', start_pos + 1);
                        const auto slash_end_pos = regex.find('/', start_pos);
                        const auto end_pos       = std::min(slash_end_pos, next_end_pos);
                        const auto key_name = regex.substr(start_pos + 1, end_pos - start_pos - 1);

                        if (key_name.empty()) {
                            throw std::runtime_error(std::string("Empty parameter name "
                                                                 "found "
                                                                 "in path: ")
                                                     + path.data());
                        }

                        if (end_pos == next_end_pos) {
                            regex.replace(start_pos, end_pos - start_pos + 1, "(.+)");
                        } else {
                            regex.replace(start_pos, end_pos - start_pos, "([^/\\s]+)");
                        }
                        keys_.emplace_back(key_name);
                    }
                }
                if (whole) {
                    regex += "$";
                }
                re_ = std::regex(regex);
            }

            std::tuple<bool, kv_t> operator()(std::string_view raw) {
                std::cmatch cm;
                auto p = static_cast<const char*>(raw.data());
                if (!std::regex_match(p, p + raw.size(), cm, re_)) {
                    return std::make_tuple(false, nullptr);
                }

                if (cm.size() - 1 != keys_.size()) {
                    return std::make_tuple(false, nullptr);
                }

                auto params = std::make_shared<std::unordered_map<std::string, std::string>>();
                int i       = 1;
                for (auto key : keys_) {
                    params->emplace(std::move(key), cm[i++].str());
                }
                return std::make_tuple(true, params);
            }

        private:
            std::regex re_;
            std::vector<std::string> keys_;
        };

        ParsePath pp;
        pp.init(path, whole);
        return pp;
    }

private:
    std::vector<http_handle_t> handlers_;
};

}  // namespace lit
}  // namespace cc
