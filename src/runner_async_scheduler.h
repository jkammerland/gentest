#pragma once

#include "async_scheduler_core.h"
#include "gentest/detail/runtime_context.h"
#include "runner_async_state.h"
#include "runner_async_status_renderer.h"

#include <chrono>
#include <coroutine>
#include <cstddef>
#include <functional>
#include <memory>
#include <source_location>
#include <string>
#include <unordered_set>
#include <vector>

namespace gentest::runner {

class BatchAsyncScheduler final : public gentest::detail::AsyncScheduler {
  public:
    using StopCallback     = std::function<bool()>;
    using ProgressCallback = std::function<bool()>;

    BatchAsyncScheduler(std::vector<AsyncCaseRun> &runs, AsyncStatusRenderer *renderer);
    ~BatchAsyncScheduler() override;

    void               post(std::coroutine_handle<> handle) override;
    void               post_frame(const gentest::detail::AsyncFramePtr &frame) override;
    void               block(std::coroutine_handle<> handle, std::string reason) override;
    void               block_at(std::coroutine_handle<> handle, std::string reason, const std::source_location &loc) override;
    void               yield_at(std::coroutine_handle<> handle, const std::source_location &loc) override;
    void               attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) override;
    void               attach_child_frame(const gentest::detail::AsyncFramePtr &child, std::coroutine_handle<> parent) override;
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
    [[nodiscard]] auto owner_for(std::coroutine_handle<> handle) const -> std::size_t;
    void               register_adopted_release_wake_for(std::size_t run_index);
    [[nodiscard]] auto format_cannot_resume_message(const AsyncSchedulerCore::SuspendedState &state) const -> std::string;
    [[nodiscard]] bool run_is_complete(std::size_t owner) const;
    void               complete(std::size_t owner);
    [[nodiscard]] auto resume_one_ready() -> bool;
    [[nodiscard]] auto has_unfinished_adopted_work() const -> bool;
    void wait_for_ready_or_adopted_release(const StopCallback &should_stop, const StopCallback &should_stop_waiting_for_adopted);
    [[nodiscard]] auto drain_ready_and_adopted_work(const StopCallback &should_stop, const ProgressCallback &after_progress,
                                                    const StopCallback &should_stop_waiting_for_adopted) -> bool;

    std::vector<AsyncCaseRun>                                            &runs_;
    AsyncStatusRenderer                                                  *renderer_ = nullptr;
    std::shared_ptr<gentest::detail::TestContextInfo::AdoptedReleaseWake> adopted_release_wake_;
    std::unordered_set<gentest::detail::TestContextInfo *>                context_listener_contexts_;
    AsyncSchedulerCore                                                    core_;
};

} // namespace gentest::runner
