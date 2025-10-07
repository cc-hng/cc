#pragma once

#include <cc/asio/pool.h>
#include <cc/util.h>

#ifdef CC_ENABLE_COROUTINE
#    include <cc/asio/channel.h>
#    include <cc/asio/condvar.h>
#    include <cc/asio/helper.h>
#    include <cc/asio/semaphore.h>

namespace cc {
namespace detail {

template <typename T>
struct typeone<net::awaitable<T>> {
    static std::string name() { return fmt::format("awaitable<{}>", typeone<T>::name()); }
};

}  // namespace detail
}  // namespace cc

#endif
