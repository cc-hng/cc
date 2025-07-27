#pragma once

#include <functional>
#include <regex>
#include <string_view>
#include <variant>
#include <vector>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/noncopyable.hpp>
#include <cc/lit/object.h>
#include <gsl/gsl>

namespace cc {
namespace lit {

template <                                    //
    typename ReqBody  = http_request_body_t,  //
    typename RespBody = http_response_body_t>
class Router : boost::noncopyable {
public:
    using request_type  = http_request_t<ReqBody>;
    using response_type = http_response_t<RespBody>;
    using kv_t          = std::optional<typename request_type::kv_t>;

    using next_handler  = http_next_handler;
    using route_handler = http_route_handler<ReqBody, RespBody>;

public:
    Router() { handlers_.reserve(64); }

    ~Router() {}

    Router& use(route_handler&& h) {
        handlers_.emplace_back((route_handler&&)h);
        return *this;
    }

    Router& route(http::verb method, std::string_view path, const route_handler& h) {
        class Functor {
        public:
            Functor(http::verb method, std::string_view path, const route_handler& handler)
              : method_(method)
              , parse_path_(compile_route(path))
              , handler_(handler) {}

            net::awaitable<void>
            operator()(const request_type& req, response_type& resp, const next_handler& go) {
                if (method_ != (http::verb)-1) {
                    if (req->method() != method_) {
                        co_return co_await go();
                    }
                }

                auto [matched, params] = parse_path_(req.path);
                if (!matched) {
                    co_return co_await go();
                }

                req.params = std::move(params);
                co_await handler_(req, resp, go);
            }

        private:
            http::verb method_;
            std::function<std::tuple<bool, kv_t>(std::string_view)> parse_path_;
            route_handler handler_;
        };

        return use(Functor(method, path, h));
    }

    net::awaitable<void>  //
    process(const request_type& req, response_type& resp, const next_handler& go) {
        try {
            co_await process_one(0, req, resp, go);
        } catch (std::exception& e) {
            resp->result(http::status::internal_server_error);
            resp->body() = std::string(e.what()) + "\n";
            resp->prepare_payload();
        }
    }

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
                bool is_regex                         = false;
                std::string regex                     = std::string(path);
                std::string_view::size_type start_pos = 0;
                while (start_pos != std::string_view::npos) {
                    start_pos = regex.find(':', start_pos);
                    if (start_pos != std::string_view::npos) {
                        const auto next_end_pos  = regex.find(':', start_pos + 1);
                        const auto slash_end_pos = regex.find('/', start_pos);
                        const auto end_pos       = std::min(slash_end_pos, next_end_pos);
                        const auto key_name = regex.substr(start_pos + 1, end_pos - start_pos - 1);

                        if (GSL_UNLIKELY(key_name.empty())) {
                            throw std::runtime_error(std::string("Empty parameter name "
                                                                 "found "
                                                                 "in path: ")
                                                     + path.data());
                        }

                        if (next_end_pos != std::string_view::npos && end_pos == next_end_pos) {
                            regex.replace(start_pos, end_pos - start_pos + 1, "(.+)");
                        } else {
                            regex.replace(start_pos, end_pos - start_pos, "([^/\\s]+)");
                        }
                        keys_.emplace_back(key_name);
                        is_regex = true;
                    }
                }
                regex_str_ = regex;
                if (is_regex) {
                    regex = "^" + regex;
                    if (whole) {
                        regex += "$";
                    }
                    re_ = std::make_shared<std::regex>(regex);
                }
            }

            std::tuple<bool, kv_t> operator()(std::string_view raw) {
                if (!re_) {
                    return std::make_tuple(std::string_view(regex_str_) == raw, std::nullopt);
                }

                std::cmatch cm;
                auto p = static_cast<const char*>(raw.data());
                if (!std::regex_match(p, p + raw.size(), cm, *re_)) {
                    return std::make_tuple(false, std::nullopt);
                }

                if (cm.size() - 1 != keys_.size()) {
                    return std::make_tuple(false, std::nullopt);
                }

                typename kv_t::value_type params;
                int i = 1;
                for (auto key : keys_) {
                    params.emplace(std::move(key), cm[i++].str());
                }
                return std::make_tuple(true, params);
            }

        private:
            std::string regex_str_;
            std::shared_ptr<std::regex> re_ = nullptr;
            std::vector<std::string> keys_;
        };

        ParsePath pp;
        pp.init(path, whole);
        return std::move(pp);
    }

private:
    net::awaitable<void>  //
    process_one(int index, const request_type& req, response_type& resp, const next_handler& go) {
        if (GSL_UNLIKELY(index >= handlers_.size())) {
            if (go) {
                co_await go();
            }
            co_return;
        }

        const auto& fn = handlers_.at(index);
        auto next      = [&, this, index]() -> net::awaitable<void> {
            co_await process_one(index + 1, req, resp, nullptr);
        };
        co_await fn(req, resp, next);
    }

private:
    std::vector<route_handler> handlers_;
};

}  // namespace lit
}  // namespace cc
