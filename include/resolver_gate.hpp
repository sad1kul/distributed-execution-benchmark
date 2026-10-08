#pragma once

#include <atomic>

namespace bom::net::detail {

// System DNS calls have no portable cancellation API. This gate ensures a timed-out
// lookup cannot be followed by an unbounded number of lingering resolver threads.
class ResolverGate {
public:
    bool try_acquire() noexcept {
        bool expected = false;
        return active_.compare_exchange_strong(expected, true);
    }

    void release() noexcept {
        active_.store(false);
    }

    [[nodiscard]] bool active() const noexcept {
        return active_.load();
    }

private:
    std::atomic<bool> active_{false};
};

}  // namespace bom::net::detail
