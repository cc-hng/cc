#pragma once

#include <boost/beast/zlib.hpp>
#include <cc/lit/object.h>

namespace cc {
namespace lit {
namespace middleware {

namespace detail {

static constexpr size_t kMinDeflateSize = 64 * 1024;

}

namespace zlib = boost::beast::zlib;

template <typename ReqBody, typename RespBody>
net::awaitable<void>  //
auto_headers(const http_request_t<ReqBody>& req, http_response_t<RespBody>& resp,
             const http_next_handler& go) {
    resp->result(http::status::not_found);
    resp->version(req->version());
    resp->set(http::field::server, BOOST_BEAST_VERSION_STRING);
    resp->set(http::field::accept_ranges, "bytes");
    resp->keep_alive(req->keep_alive());
    resp->body() = "";

    co_await go();

    if (resp->result() == http::status::not_found) {
        resp->body() = "Not found\n";
    }
    if (resp->find(http::field::content_type) == resp->end()) {
        resp->set(http::field::content_type, "text/plain");
    }

    if (req->method() != http::verb::head) {
        auto& body        = resp->body();
        auto& f           = resp->base();
        auto content_type = f[http::field::content_type];
        // deflate
        //  1. sizeof(response_body) > kMinDeflateSize
        //  2. Request header: Accept Encoding: "deflate"
        //  3. Content Type: !video !audio
        auto it = req->find(http::field::accept_encoding);
        if (body.size() >= detail::kMinDeflateSize && (it != req->end())
            && !(content_type.find("video") != std::string_view::npos
                 || content_type.find("audio") != std::string_view::npos)) {
            if (it->value().find("deflate") != std::string_view::npos) {
                zlib::z_params zs;
                zlib::deflate_stream ds;
                ds.reset(zlib::compression::default_size, 15, 4, zlib::Strategy::fixed);
                std::string out;
                out.resize(zlib::deflate_upper_bound(body.size()));
                zs.next_in   = body.data();
                zs.avail_in  = body.size();
                zs.next_out  = &out[0];
                zs.avail_out = out.size();
                boost::beast::error_code ec;
                ds.write(zs, zlib::Flush::full, ec);
                // BEAST_EXPECTS(!ec, ec.message());
                out.resize(zs.total_out);
                body = std::move(out);
                resp->set(http::field::content_encoding, "deflate");
            }
        }

        resp->prepare_payload();
    }
}

template <typename ReqBody, typename RespBody>
net::awaitable<void>  //
cors(const http_request_t<ReqBody>& req, http_response_t<RespBody>& resp,
     const http_next_handler& go) {
    if (req->method() == http::verb::options) {
        resp.set_content("", "text/plain");
    }

    co_await go();

    resp->set("Access-Control-Allow-Origin", "*");
    resp->set("Access-Control-Allow-Headers", "*");
    resp->set("Access-Control-Max-Age", "86400");
    resp->set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
}

}  // namespace middleware
}  // namespace lit
}  // namespace cc
