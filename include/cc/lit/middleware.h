#pragma once

#include <list>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <cc/lit/object.h>
#include <cc/stopwatch.h>

#include <stdio.h>

namespace cc {
namespace lit {

namespace detail {
class CircuitBreaker {
public:
    struct option_t {
        double duration;
        int times;
    };

    CircuitBreaker(double duration, int times) : option_({.duration = duration, .times = times}) {}
    explicit CircuitBreaker(const option_t& opt) : option_(opt) {}
    ~CircuitBreaker() {}

    boost::asio::awaitable<void>
    operator()(const lit::http_request_t& req, lit::http_response_t& resp,
               const lit::http_next_handle_t& go) {
        auto [path0, _] = lit::http_request_t::split_target(req->target());
        std::string path(path0);
        if (data_.find(path) == data_.end()) {
            std::list<cc::StopWatch> ra;
            ra.emplace_back(cc::StopWatch());
            data_.emplace(std::move(path), std::move(ra));
        } else {
            auto& ra = data_.at(path);
            if (ra.size() == option_.times) {
                if (ra.front().elapsed() < option_.duration) {
                    resp->result(boost::beast::http::status::forbidden);
                    co_return;
                } else {
                    ra.pop_front();
                }
            }
            ra.push_back(cc::StopWatch());
        }
        co_await go();
    }

private:
    const option_t option_;
    std::unordered_map<std::string, std::list<cc::StopWatch>> data_;
};
}  // namespace detail

namespace http = boost::beast::http;

// as namespace
struct middleware {
    static boost::asio::awaitable<void>
    auto_headers(const http_request_t& req, http_response_t& resp, const http_next_handle_t& go) {
        resp->version(req->version());
        resp->set(http::field::server, BOOST_BEAST_VERSION_STRING);

        co_await go();

        resp->keep_alive(req->keep_alive());
        if (resp->find(http::field::content_type) == resp->end()) {
            resp->set(http::field::content_type, "text/plain");
        }
    }

    static boost::asio::awaitable<void> logger(const auto& req, auto& resp, const auto& go) {
        fprintf(stderr, "logger begin ... \n");
        co_await go();
        fprintf(stderr, "logger  end  ... \n");
    }

    static boost::asio::awaitable<void>
    cors(const http_request_t& req, http_response_t& resp, const http_next_handle_t& go) {
        if (req->method() == boost::beast::http::verb::options) {
            resp.set_content("");
        }

        co_await go();
        resp->set("Access-Control-Allow-Origin", "*");
        resp->set("Access-Control-Allow-Headers", "*");
        resp->set("Access-Control-Max-Age", "86400");
        resp->set("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    }

    static decltype(auto) make_circuit_breaker(double duration, int times) {
        return detail::CircuitBreaker(duration, times);
    }
};

}  // namespace lit
}  // namespace cc
