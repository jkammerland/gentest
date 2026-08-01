#pragma once

#include "async_timer_queue.h"
#include "gentest/async.h"

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <source_location>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gentest::runner {

class AsyncSchedulerCore {
  public:
    enum class SuspendKind { Suspended, Yielded };

    struct SuspendedState {
        SuspendKind   kind = SuspendKind::Suspended;
        std::string   reason;
        std::string   file;
        unsigned      line     = 0;
        std::uint64_t sequence = 0;
    };

    using FramePtr       = gentest::detail::AsyncFramePtr;
    using WaiterTokenPtr = gentest::detail::AsyncScheduler::WaiterTokenPtr;
    using WakeCallback   = std::function<void()>;

    static constexpr std::size_t kInvalidOwner = std::numeric_limits<std::size_t>::max();

    explicit AsyncSchedulerCore(WakeCallback wake = {}) : wake_(std::move(wake)) {}

    void set_wake_callback(WakeCallback wake) { wake_ = std::move(wake); }

    void register_frame(std::size_t owner, const FramePtr &frame) {
        if (!frame || !frame->handle) {
            return;
        }
        std::lock_guard<std::mutex> lk(mtx_);
        frames_[frame->address()] = frame;
        owners_[frame->address()] = owner;
    }

    [[nodiscard]] auto owner_for(std::coroutine_handle<> handle) const -> std::size_t {
        std::lock_guard<std::mutex> lk(mtx_);
        return owner_for_locked(handle.address());
    }

    [[nodiscard]] auto owner_for(const FramePtr &frame) const -> std::size_t {
        if (!frame) {
            return kInvalidOwner;
        }
        std::lock_guard<std::mutex> lk(mtx_);
        return owner_for_locked(frame->address());
    }

    [[nodiscard]] auto make_waiter(std::coroutine_handle<> handle, const std::shared_ptr<gentest::detail::AsyncScheduler::Control> &control)
        -> WaiterTokenPtr {
        FramePtr frame;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            frame = frame_for_locked(handle.address());
        }

        auto token = std::make_shared<gentest::detail::AsyncScheduler::WaiterToken>(control, frame);
        if (!frame || frame->is_canceled()) {
            token->cancel();
            return token;
        }

        std::lock_guard<std::mutex> lk(mtx_);
        if (owner_for_locked(frame->address()) == kInvalidOwner) {
            token->cancel();
            return token;
        }
        waiter_tokens_[frame->address()].push_back(token);
        return token;
    }

    void post(std::coroutine_handle<> handle) {
        FramePtr frame;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            frame = frame_for_locked(handle.address());
        }
        post(std::move(frame));
    }

    void post(FramePtr frame) {
        if (!frame) {
            return;
        }

        bool should_wake = false;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            const auto                  address = frame->address();
            if (owner_for_locked(address) == kInvalidOwner) {
                blocked_.erase(address);
                return;
            }
            if (frame->is_canceled() || frame->done()) {
                blocked_.erase(address);
                return;
            }
            blocked_.erase(address);
            ready_.push_back(std::move(frame));
            should_wake = true;
        }
        notify_wake(should_wake);
    }

    void block(std::coroutine_handle<> handle, std::string reason, const std::source_location &loc = {}) {
        block(handle, SuspendKind::Suspended, std::move(reason), loc);
    }

    void block(std::coroutine_handle<> handle, SuspendKind kind, std::string reason, const std::source_location &loc = {}) {
        if (!handle) {
            return;
        }
        std::lock_guard<std::mutex> lk(mtx_);
        const auto                  owner = owner_for_locked(handle.address());
        if (owner == kInvalidOwner) {
            return;
        }
        blocked_[handle.address()] = BlockedHandle{.owner    = owner,
                                                   .kind     = kind,
                                                   .sequence = ++suspend_sequence_,
                                                   .reason   = reason.empty() ? std::string("async test cannot resume") : std::move(reason),
                                                   .file     = loc.file_name() == nullptr ? std::string{} : std::string(loc.file_name()),
                                                   .line     = loc.line()};
    }

    void yield(std::coroutine_handle<> handle, const std::source_location &loc) {
        if (!handle) {
            return;
        }

        bool should_wake = false;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            auto                        frame = frame_for_locked(handle.address());
            const auto                  owner = owner_for_locked(handle.address());
            if (!frame || owner == kInvalidOwner || frame->is_canceled() || frame->done()) {
                return;
            }
            blocked_[handle.address()] = BlockedHandle{.owner    = owner,
                                                       .kind     = SuspendKind::Yielded,
                                                       .sequence = ++suspend_sequence_,
                                                       .reason   = "yielded cooperatively",
                                                       .file = loc.file_name() == nullptr ? std::string{} : std::string(loc.file_name()),
                                                       .line = loc.line()};
            ready_.push_back(std::move(frame));
            should_wake = true;
        }
        notify_wake(should_wake);
    }

    void attach_child(const FramePtr &child, std::coroutine_handle<> parent) {
        if (!child || !child->handle || !parent) {
            return;
        }
        std::lock_guard<std::mutex> lk(mtx_);
        const auto                  parent_owner = owner_for_locked(parent.address());
        if (parent_owner == kInvalidOwner) {
            return;
        }
        frames_[child->address()] = child;
        owners_[child->address()] = parent_owner;
        auto &children            = children_[parent.address()];
        auto &parents             = parents_[child->address()];
        children.push_back(ChildReference{
            .child             = child->address(),
            .parent_link_index = parents.size(),
        });
        parents.push_back(ParentReference{
            .parent      = parent.address(),
            .child_index = children.size() - 1,
        });
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) - Mirrors the public scheduler hook.
    void attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) {
        if (!child) {
            return;
        }
        FramePtr child_frame;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            child_frame = frame_for_locked(child.address());
        }
        attach_child(child_frame, parent);
    }

    void schedule_timer(std::chrono::steady_clock::time_point deadline, const WaiterTokenPtr &token) {
        if (!token) {
            return;
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            timers_.push(deadline, token);
        }
        notify_wake(true);
    }

    void post_due_timers() {
        std::vector<WaiterTokenPtr> due;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            due = timers_.pop_due(std::chrono::steady_clock::now());
        }
        for (auto &token : due) {
            token->post();
        }
    }

    [[nodiscard]] auto next_timer_deadline() -> std::optional<std::chrono::steady_clock::time_point> {
        std::lock_guard<std::mutex> lk(mtx_);
        return timers_.next_deadline();
    }

    [[nodiscard]] auto has_pending_timers() -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        return timers_.has_pending();
    }

    [[nodiscard]] auto ready_size() const -> std::size_t {
        std::lock_guard<std::mutex> lk(mtx_);
        return ready_.size();
    }

    [[nodiscard]] auto has_ready() const -> bool {
        std::lock_guard<std::mutex> lk(mtx_);
        return !ready_.empty();
    }

    [[nodiscard]] auto pop_ready() -> FramePtr {
        std::lock_guard<std::mutex> lk(mtx_);
        while (!ready_.empty()) {
            auto frame = std::move(ready_.front());
            ready_.pop_front();
            if (frame && !frame->is_canceled()) {
                return frame;
            }
        }
        return {};
    }

    void clear_suspend_state(const FramePtr &frame) {
        if (!frame) {
            return;
        }
        std::lock_guard<std::mutex> lk(mtx_);
        blocked_.erase(frame->address());
    }

    void cancel_owner(std::size_t owner) {
        cancel_matching([&](std::size_t frame_owner) { return frame_owner == owner; });
    }

    void cancel_waiters(std::coroutine_handle<> handle) {
        if (!handle) {
            return;
        }
        cancel_address(handle.address());
    }

    void cancel_all() {
        cancel_matching([](std::size_t) { return true; });
    }

    [[nodiscard]] auto suspended_state_for(std::size_t owner) const -> SuspendedState {
        SuspendedState              result{.reason = "waiting to resume"};
        std::lock_guard<std::mutex> lk(mtx_);
        for (const auto &[_, blocked] : blocked_) {
            if (blocked.owner == owner && !blocked.reason.empty() && blocked.sequence >= result.sequence) {
                result = SuspendedState{
                    .kind     = blocked.kind,
                    .reason   = blocked.reason,
                    .file     = blocked.file,
                    .line     = blocked.line,
                    .sequence = blocked.sequence,
                };
            }
        }
        return result;
    }

    [[nodiscard]] auto first_blocked_reason() const -> std::string {
        std::lock_guard<std::mutex> lk(mtx_);
        if (blocked_.empty()) {
            return {};
        }
        return blocked_.begin()->second.reason;
    }

  private:
    struct ChildReference {
        void       *child             = nullptr;
        std::size_t parent_link_index = 0;
    };

    struct ParentReference {
        void       *parent      = nullptr;
        std::size_t child_index = 0;
    };

    struct BlockedHandle {
        std::size_t   owner    = 0;
        SuspendKind   kind     = SuspendKind::Suspended;
        std::uint64_t sequence = 0;
        std::string   reason;
        std::string   file;
        unsigned      line = 0;
    };

    [[nodiscard]] auto owner_for_locked(void *address) const -> std::size_t {
        const auto it = owners_.find(address);
        if (it == owners_.end()) {
            return kInvalidOwner;
        }
        return it->second;
    }

    [[nodiscard]] auto frame_for_locked(void *address) const -> FramePtr {
        const auto it = frames_.find(address);
        if (it == frames_.end()) {
            return {};
        }
        return it->second;
    }

    template <typename Pred> void cancel_matching(Pred pred) {
        std::vector<WaiterTokenPtr> tokens;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            std::vector<void *>         addresses;
            for (const auto &[address, owner] : owners_) {
                if (pred(owner)) {
                    addresses.push_back(address);
                }
            }
            for (auto *address : addresses) {
                cancel_address_locked(address, tokens);
            }
        }
        cancel_tokens(tokens);
    }

    void cancel_address(void *address) {
        std::vector<WaiterTokenPtr> tokens;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            cancel_address_locked(address, tokens);
        }
        cancel_tokens(tokens);
    }

    void cancel_address_locked(void *address, std::vector<WaiterTokenPtr> &tokens) {
        if (auto frame = frame_for_locked(address)) {
            frame->cancel();
        }

        if (const auto child_it = children_.find(address); child_it != children_.end()) {
            auto children = std::move(child_it->second);
            children_.erase(child_it);
            for (const auto &child : children) {
                cancel_address_locked(child.child, tokens);
            }
        }
        remove_child_references_locked(address);

        const auto waiter_it = waiter_tokens_.find(address);
        if (waiter_it != waiter_tokens_.end()) {
            for (auto &weak_waiter : waiter_it->second) {
                if (auto waiter = weak_waiter.lock()) {
                    tokens.push_back(std::move(waiter));
                }
            }
            waiter_tokens_.erase(waiter_it);
        }

        blocked_.erase(address);
        owners_.erase(address);
        frames_.erase(address);
        std::erase_if(ready_, [address](const FramePtr &frame) { return !frame || frame->address() == address; });
    }

    void remove_child_references_locked(void *address) {
        const auto parent_it = parents_.find(address);
        if (parent_it == parents_.end()) {
            return;
        }

        auto parents = std::move(parent_it->second);
        parents_.erase(parent_it);
        for (const auto &parent : parents) {
            const auto children_it = children_.find(parent.parent);
            if (children_it == children_.end()) {
                continue;
            }
            auto &children = children_it->second;
            if (parent.child_index >= children.size() || children[parent.child_index].child != address) {
                continue;
            }

            const std::size_t last_index = children.size() - 1;
            if (parent.child_index != last_index) {
                const auto moved_child       = children[last_index];
                children[parent.child_index] = moved_child;
                if (const auto moved_parent_it = parents_.find(moved_child.child);
                    moved_parent_it != parents_.end() && moved_child.parent_link_index < moved_parent_it->second.size()) {
                    moved_parent_it->second[moved_child.parent_link_index].child_index = parent.child_index;
                }
            }
            children.pop_back();
            if (children.empty()) {
                children_.erase(children_it);
            }
        }
    }

    static void cancel_tokens(std::vector<WaiterTokenPtr> &tokens) {
        for (auto &token : tokens) {
            token->cancel();
        }
    }

    void notify_wake(bool should_wake) {
        if (should_wake && wake_) {
            wake_();
        }
    }

    WakeCallback                                                                         wake_;
    mutable std::mutex                                                                   mtx_;
    std::deque<FramePtr>                                                                 ready_;
    std::unordered_map<void *, FramePtr>                                                 frames_;
    std::unordered_map<void *, std::size_t>                                              owners_;
    std::unordered_map<void *, std::vector<ChildReference>>                              children_;
    std::unordered_map<void *, std::vector<ParentReference>>                             parents_;
    std::unordered_map<void *, BlockedHandle>                                            blocked_;
    std::unordered_map<void *, std::vector<std::weak_ptr<WaiterTokenPtr::element_type>>> waiter_tokens_;
    gentest::detail::AsyncTimerQueue                                                     timers_;
    std::uint64_t                                                                        suspend_sequence_ = 0;
};

} // namespace gentest::runner
