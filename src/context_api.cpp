#include "gentest/context.h"
#include "gentest/detail/runtime_context.h"

#include <mutex>
#include <string>
#include <vector>

namespace gentest {
namespace detail {

struct CurrentContextAccess {
    [[nodiscard]] static auto get(const CurrentContext &context) noexcept -> const std::shared_ptr<TestContextInfo> & {
        return context.ctx_;
    }
};

} // namespace detail

namespace {

[[nodiscard]] auto context_info(const CurrentContext &context) noexcept -> const std::shared_ptr<detail::TestContextInfo> & {
    return detail::CurrentContextAccess::get(context);
}

void acquire_adopted_context(const CurrentContext &context) {
    if (!context) {
        return;
    }
    const auto                 &ctx = context_info(context);
    std::lock_guard<std::mutex> lk(ctx->adopted_mtx);
    ctx->adopted_contexts.fetch_add(1, std::memory_order_acq_rel);
}

[[nodiscard]] auto release_adopted_context(const CurrentContext &context) noexcept -> bool {
    if (!context) {
        return false;
    }
    const auto                 &ctx = context_info(context);
    std::lock_guard<std::mutex> lk(ctx->adopted_mtx);
    return ctx->adopted_contexts.fetch_sub(1, std::memory_order_acq_rel) == 1;
}

} // namespace

auto CurrentContext::stop_token() const noexcept -> std::stop_token {
    if (!ctx_) {
        return {};
    }
    return ctx_->stop_source.get_token();
}

auto CurrentContext::stop_requested() const noexcept -> bool { return stop_token().stop_requested(); }

auto get_current_context() -> CurrentContext { return CurrentContext(detail::current_test()); }

auto set_current_context(CurrentContext context) -> CurrentContextLease { return CurrentContextLease(std::move(context)); }

auto get_current_token() -> CurrentToken { return get_current_context(); }

auto set_current_token(CurrentToken context) -> CurrentContextLease { return set_current_context(std::move(context)); }

CurrentContextLease::CurrentContextLease(CurrentContext context)
    : previous_(get_current_context()), leased_(std::move(context)), previous_role_(detail::current_context_role()) {
    acquire_adopted_context(leased_);
    try {
        detail::set_current_test(context_info(leased_), detail::CurrentContextRole::Adopted);
    } catch (...) {
        if (release_adopted_context(leased_)) {
            detail::notify_adopted_contexts_released(*context_info(leased_));
        }
        throw;
    }
}

CurrentContextLease::~CurrentContextLease() {
    detail::set_current_test(context_info(previous_), previous_role_);
    if (release_adopted_context(leased_)) {
        detail::close_context_if_released(*context_info(leased_));
        detail::notify_adopted_contexts_released(*context_info(leased_));
    }
}

void log(std::string_view message) {
    auto  ctx    = detail::current_test_storage();
    auto &buffer = detail::current_buffer_storage();
    if (!detail::accepts_late_test_operation(ctx)) {
        detail::fail_without_active_context("log called");
    }
    if (buffer.owner != ctx.get()) {
        detail::flush_current_buffer_for(buffer.owner);
        buffer.owner = ctx.get();
    }

    std::vector<std::string>  recent_logs;
    std::size_t               log_count      = 0;
    void                     *observer_state = nullptr;
    detail::TestLogObserverFn observer       = nullptr;
    std::size_t               observer_id    = 0;
    buffer.logs.emplace_back(message);
    {
        std::lock_guard<std::mutex> lk(ctx->mtx);
        ++ctx->log_count;
        log_count = ctx->log_count;
        if (ctx->recent_log_limit != 0) {
            ctx->recent_logs.emplace_back(message);
            while (ctx->recent_logs.size() > ctx->recent_log_limit) {
                ctx->recent_logs.erase(ctx->recent_logs.begin());
            }
        } else {
            ctx->recent_logs.clear();
        }
        recent_logs    = ctx->recent_logs;
        observer_state = ctx->log_observer_state;
        observer       = ctx->log_observer;
        observer_id    = ctx->log_observer_id;
    }
    if (observer) {
        observer(observer_state, observer_id, recent_logs, log_count);
    }
    detail::dispatch_log_to_sinks(message);
}

[[noreturn]] void skip(std::string_view reason, const std::source_location &loc) {
    (void)loc;
    detail::request_runtime_skip(reason, detail::TestContextInfo::RuntimeSkipKind::User);
#if GENTEST_EXCEPTIONS_ENABLED
    throw detail::skip_exception{};
#else
    ::gentest::detail::terminate_no_exceptions_fatal("gentest::skip");
#endif
}

void xfail(std::string_view reason, const std::source_location &loc) {
    (void)loc;
    detail::require_owner_context("xfail called");
    auto                        ctx = detail::current_test_storage();
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->xfail_requested = true;
    if (!reason.empty()) {
        ctx->xfail_reason = std::string(reason);
    }
}

} // namespace gentest
