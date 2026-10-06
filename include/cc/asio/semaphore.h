#pragma once

#include <cstddef>
#include <optional>
#include <cc/asio/condvar.h>
#include <cc/asio/helper.h>
#include <cc/util.h>

namespace cc {

template <typename MutexPolicy = NonMutex,  //
          template <class> class WriterLock = LockGuard>
class Semaphore final {
    MutexPolicy mtx_;
    CondVar<MutexPolicy> cv_;
    std::size_t permits_;

public:
    explicit Semaphore(std::size_t init_permits) : permits_(init_permits) {}
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    net::awaitable<void> acquire() {
        for (;;) {
            // waitSince (inside wait_claim) propagates cancellation; a cancelled
            // acquire unwinds instead of re-arming a wait that can never fire again.
            auto claimed = co_await detail::wait_claim<WriterLock>(mtx_, cv_, [this]() -> std::optional<bool> {
                if (permits_ > 0) {
                    permits_--;
                    return true;
                }
                return std::nullopt;
            });
            if (claimed) {
                co_return;
            }
        }
    }

    inline void release() {
        {
            WriterLock<MutexPolicy> _lck{mtx_};
            // ponytail: permit overflow intentionally unchecked; validate a max count if unbalanced release is
            // needed.
            permits_++;
        }
        // ponytail: notifyAll avoids stranding a permit if selected waiter is canceled;
        // switch to notifyOne after cancellation handoff is implemented.
        cv_.notifyAll();
    }
};

}  // namespace cc
