#pragma once

#include "gentest/async.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gentest::detail {

class AsyncTimerQueue {
  public:
    using time_point     = std::chrono::steady_clock::time_point;
    using WaiterTokenPtr = gentest::detail::AsyncScheduler::WaiterTokenPtr;

    // Returns tokens retired by compaction. The caller destroys them after
    // releasing its lock, so no token destructor runs under the scheduler mutex.
    [[nodiscard]] auto push(time_point deadline, const WaiterTokenPtr &token) -> std::vector<WaiterTokenPtr> {
        std::vector<WaiterTokenPtr> retired;
        if (!token) {
            return retired;
        }
        // A completed or cancelled wait is only popped once it reaches the top,
        // which an earlier live timer can postpone indefinitely. Sweep whenever
        // the heap doubles since the last sweep; stale entries stay bounded by
        // the live ones and the amortized cost per push stays constant.
        if (timers_.size() >= compact_at_) {
            retired = compact();
        }
        timers_.push_back(Timer{.deadline = deadline, .sequence = next_sequence_++, .token = token});
        std::ranges::push_heap(timers_, EarlierDeadlineFirst{});
        return retired;
    }

    [[nodiscard]] auto pop_due(time_point now) -> std::vector<WaiterTokenPtr> {
        std::vector<WaiterTokenPtr> due;
        while (!timers_.empty()) {
            auto token = timers_.front().token;
            if (!token || !token->active()) {
                pop();
                continue;
            }
            if (timers_.front().deadline > now) {
                break;
            }
            pop();
            due.push_back(std::move(token));
        }
        return due;
    }

    [[nodiscard]] auto next_deadline() -> std::optional<time_point> {
        purge_stale();
        if (timers_.empty()) {
            return std::nullopt;
        }
        return timers_.front().deadline;
    }

    [[nodiscard]] auto has_pending() -> bool { return next_deadline().has_value(); }

    // Entries currently held, including stale ones awaiting compaction.
    [[nodiscard]] auto size() const noexcept -> std::size_t { return timers_.size(); }

    void clear() {
        timers_.clear();
        compact_at_ = minimum_compaction_size;
    }

  private:
    static constexpr std::size_t minimum_compaction_size = 16;

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

    void pop() {
        std::ranges::pop_heap(timers_, EarlierDeadlineFirst{});
        timers_.pop_back();
    }

    void purge_stale() {
        while (!timers_.empty()) {
            const auto &next = timers_.front();
            if (next.token && next.token->active()) {
                return;
            }
            pop();
        }
    }

    [[nodiscard]] auto compact() -> std::vector<WaiterTokenPtr> {
        std::vector<WaiterTokenPtr> retired;
        const auto stale = std::ranges::partition(timers_, [](const Timer &timer) { return timer.token && timer.token->active(); });
        retired.reserve(stale.size());
        for (auto &timer : stale) {
            retired.push_back(std::move(timer.token));
        }
        timers_.erase(stale.begin(), stale.end());
        std::ranges::make_heap(timers_, EarlierDeadlineFirst{});
        compact_at_ = std::max(minimum_compaction_size, timers_.size() * 2);
        return retired;
    }

    std::vector<Timer> timers_;
    std::size_t        compact_at_    = minimum_compaction_size;
    std::uint64_t      next_sequence_ = 0;
};

} // namespace gentest::detail
