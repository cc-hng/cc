#pragma once

#include <map>
#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace lit {

namespace detail {

class FetchConnections {};

}  // namespace detail

struct fetch_option_t {
    using method_type = boost::beast::http::verb;
    fetch_option_t()  = default;

    method_type method = method_type::get;
    bool keepalive     = false;
    int timeout        = 30;  // seconds
    std::map<std::string, std::string> headers;
    std::string body;
};

boost::asio::awaitable<std::string>
fetch(std::string_view url, const fetch_option_t options = {});

}  // namespace lit
