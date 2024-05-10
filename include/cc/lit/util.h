#pragma once

#include <functional>
#include <type_traits>
#include <boost/callable_traits.hpp>
#include <boost/hana.hpp>
#include <cc/json.h>
#include <cc/lit/object.h>
#include <cc/type_traits.h>

namespace cc {
//
namespace detail {

namespace ct   = boost::callable_traits;
namespace hana = boost::hana;

class MakeRouteFunctor {
public:
    template <typename Fn, typename = hana::when<!std::is_member_function_pointer_v<Fn>>>
    constexpr decltype(auto) operator()(Fn&& fn) const {
        using namespace cc::lit;
        using Ret     = ct::return_type_t<Fn>;
        using Args    = ct::args_t<Fn, std::tuple>;
        using Request = std::decay_t<std::tuple_element_t<0, Args>>;

        return std::function(
            [fn](const http_request_t& request, http_response_t& resp,
                 const http_next_handle_t& go) -> boost::asio::awaitable<void> {
                auto body    = request->body();
                auto req_msg = cc::json::parse<Request>(body);
                std::string rep_json;
                if constexpr (cc::is_awaitable_v<Ret>) {
                    auto rep_msg = co_await fn(req_msg);
                    rep_json     = cc::json::dump(rep_msg);
                } else {
                    auto rep_msg = fn(req_msg);
                    rep_json     = cc::json::dump(rep_msg);
                }
                resp.set_content(rep_json);
                co_return;
            });
    }

    template <typename Fn, typename Self,
              typename = hana::when<std::is_member_function_pointer_v<Fn>>>
    constexpr decltype(auto) operator()(const Fn& fn, Self self) const {
        using Ret     = ct::return_type_t<Fn>;
        using Args    = ct::args_t<Fn, std::tuple>;
        using Request = std::decay_t<std::tuple_element_t<1, Args>>;

        std::function<Ret(const Request&)> f;
        if constexpr (std::is_pointer_v<Self>) {
            f = std::bind(fn, self, std::placeholders::_1);
        } else {
            constexpr auto has_get =
                hana::is_valid([](auto&& obj) -> decltype(obj.get()) {});
            BOOST_HANA_CONSTANT_CHECK(has_get(self));
            auto raw = self.get();
            f        = std::bind(fn, raw, std::placeholders::_1);
        }
        return (*this)(std::move(f));
    }
};

}  // namespace detail

[[maybe_unused]] constexpr detail::MakeRouteFunctor make_route{};

}  // namespace cc
