#pragma once

#include "async_timer_queue.h"
#include "gentest/detail/runtime_context.h"
#include "runner_async_state.h"
#include "runner_async_status_renderer.h"

#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gentest::runner {

class BatchAsyncScheduler final : public gentest::detail::AsyncScheduler {
    enum class SuspendKind { Suspended, Yielded };

  public:
    using StopCallback     = std::function<bool()>;
    using ProgressCallback = std::function<bool()>;

    BatchAsyncScheduler(std::vector<AsyncCaseRun> &runs, AsyncStatusRenderer *renderer);
    ~BatchAsyncScheduler() override;

    void               post(std::coroutine_handle<> handle) override;
    void               block(std::coroutine_handle<> handle, std::string reason) override;
    void               block_at(std::coroutine_handle<> handle, std::string reason, const std::source_location &loc) override;
    void               yield_at(std::coroutine_handle<> handle, const std::source_location &loc) override;
    void               attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) override;
    [[nodiscard]] auto make_waiter(std::coroutine_handle<> handle) -> WaiterTokenPtr override;
    void               schedule_timer(std::chrono::steady_clock::time_point deadline, const WaiterTokenPtr &token) override;
    void               cancel_waiters(std::coroutine_handle<> handle) noexcept override;

    void               add_top_level(std::size_t run_index, gentest::detail::AsyncTask &task);
    [[nodiscard]] auto run_one_ready() -> bool;
    void               run_ready();
    [[nodiscard]] auto has_ready() const noexcept -> bool;
    void               cancel_owner(std::size_t owner);
    [[nodiscard]] auto finish_unresumable(const StopCallback &should_stop = {}, const ProgressCallback &after_progress = {},
                                          const StopCallback &should_stop_waiting_for_adopted = {}) -> bool;
    void               run();

  private:
    struct BlockedHandle {
        std::size_t   owner    = 0;
        SuspendKind   kind     = SuspendKind::Suspended;
        std::uint64_t sequence = 0;
        std::string   reason;
        std::string   file;
        unsigned      line = 0;
    };

    struct SuspendedState {
        SuspendKind   kind = SuspendKind::Suspended;
        std::string   reason;
        std::string   file;
        unsigned      line     = 0;
        std::uint64_t sequence = 0;
    };

    static constexpr std::size_t kInvalidOwner = std::numeric_limits<std::size_t>::max();

    [[nodiscard]] auto owner_for(std::coroutine_handle<> handle) const -> std::size_t;
    [[nodiscard]] auto owner_for_locked(std::coroutine_handle<> handle) const -> std::size_t;
    void               register_adopted_release_wake_for(std::size_t run_index);
    [[nodiscard]] auto suspended_state_for(std::size_t owner) const -> SuspendedState;
    [[nodiscard]] auto format_cannot_resume_message(const SuspendedState &state) const -> std::string;
    [[nodiscard]] bool run_is_complete(std::size_t owner) const;
    void               complete(std::size_t owner);
    void               cancel_owner_waiters_locked(std::size_t owner, std::vector<WaiterTokenPtr> &tokens);
    void               remove_child_references_locked(void *address);
    void               cancel_one_handle_locked(std::coroutine_handle<> handle, std::vector<WaiterTokenPtr> &tokens);
    void               cancel_waiters_for_handle_locked(std::coroutine_handle<> handle, std::vector<WaiterTokenPtr> &tokens);
    void               post_due_timers();
    [[nodiscard]] auto next_timer_deadline_locked() -> std::optional<std::chrono::steady_clock::time_point>;
    [[nodiscard]] auto has_pending_timers() -> bool;
    [[nodiscard]] auto ready_size() const -> std::size_t;
    [[nodiscard]] auto ready_empty() const noexcept -> bool;
    [[nodiscard]] auto pop_ready() -> std::coroutine_handle<>;
    void               clear_suspend_state(std::coroutine_handle<> handle);
    [[nodiscard]] auto resume_one_ready() -> bool;
    [[nodiscard]] auto has_unfinished_adopted_work() const -> bool;
    void wait_for_ready_or_adopted_release(const StopCallback &should_stop, const StopCallback &should_stop_waiting_for_adopted);
    [[nodiscard]] auto drain_ready_and_adopted_work(const StopCallback &should_stop, const ProgressCallback &after_progress,
                                                    const StopCallback &should_stop_waiting_for_adopted) -> bool;

    std::vector<AsyncCaseRun>                                            &runs_;
    AsyncStatusRenderer                                                  *renderer_ = nullptr;
    mutable std::mutex                                                    mtx_;
    std::shared_ptr<gentest::detail::TestContextInfo::AdoptedReleaseWake> adopted_release_wake_;
    std::unordered_set<gentest::detail::TestContextInfo *>                context_listener_contexts_;
    std::deque<std::coroutine_handle<>>                                   ready_;
    std::unordered_map<void *, std::size_t>                               owners_;
    std::unordered_map<void *, std::vector<std::coroutine_handle<>>>      children_;
    std::unordered_map<void *, BlockedHandle>                             blocked_handles_;
    std::unordered_map<void *, std::vector<std::weak_ptr<WaiterToken>>>   waiter_tokens_;
    gentest::detail::AsyncTimerQueue                                      timers_;
    std::uint64_t                                                         suspend_sequence_ = 0;
};

} // namespace gentest::runner
