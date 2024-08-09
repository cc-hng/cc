#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <cc/lit/object.h>

#include <stdio.h>

namespace cc {
namespace lit {

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
};

}  // namespace lit
}  // namespace cc
