#pragma once

#include "gentest/async.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <queue>
#include <vector>

namespace gentest::runner {

class AsyncTimerQueue {
  public:
    using time_point     = std::chrono::steady_clock::time_point;
    using WaiterTokenPtr = gentest::detail::AsyncScheduler::WaiterTokenPtr;

    void push(time_point deadline, const WaiterTokenPtr &token) {
        if (!token) {
            return;
        }
        timers_.push(Timer{.deadline = deadline, .sequence = next_sequence_++, .token = token});
    }

    [[nodiscard]] auto pop_due(time_point now) -> std::vector<WaiterTokenPtr> {
        std::vector<WaiterTokenPtr> due;
        purge_stale();
        while (!timers_.empty() && timers_.top().deadline <= now) {
            auto token = timers_.top().token;
            timers_.pop();
            if (token && token->active()) {
                due.push_back(std::move(token));
            }
            purge_stale();
        }
        return due;
    }

    [[nodiscard]] auto next_deadline() -> std::optional<time_point> {
        purge_stale();
        if (timers_.empty()) {
            return std::nullopt;
        }
        return timers_.top().deadline;
    }

    [[nodiscard]] auto has_pending() -> bool { return next_deadline().has_value(); }

    void clear() { timers_ = {}; }

  private:
    struct Timer {
        time_point     deadline;
        std::uint64_t  sequence = 0;
        WaiterTokenPtr token;
    };

    struct EarlierDeadlineFirst {
        [[nodiscard]] auto operator()(const Timer &lhs, const Timer &rhs) const noexcept -> bool {
            if (lhs.deadline != rhs.deadline) {
                return lhs.deadline > rhs.deadline;
            }
            return lhs.sequence > rhs.sequence;
        }
    };

    void purge_stale() {
        while (!timers_.empty()) {
            const auto &next = timers_.top();
            if (next.token && next.token->active()) {
                return;
            }
            timers_.pop();
        }
    }

    std::priority_queue<Timer, std::vector<Timer>, EarlierDeadlineFirst> timers_;
    std::uint64_t                                                        next_sequence_ = 0;
};

} // namespace gentest::runner
