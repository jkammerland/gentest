#include "gentest/context.h"
#include "gentest/detail/runtime_context.h"

#include <mutex>
#include <string>

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

CurrentContextLease::CurrentContextLease(CurrentContext context) : previous_(get_current_context()), leased_(std::move(context)) {
    acquire_adopted_context(leased_);
    try {
        detail::set_current_test(context_info(leased_));
    } catch (...) {
        if (release_adopted_context(leased_)) {
            detail::notify_adopted_contexts_released(*context_info(leased_));
        }
        throw;
    }
}

CurrentContextLease::~CurrentContextLease() {
    detail::set_current_test(context_info(previous_));
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

    LogPolicy  policy            = LogPolicy::Never;
    bool       policy_overridden = false;
    const auto always_bits       = gentest::to_underlying(LogPolicy::Always);
    const auto on_failure_bits   = gentest::to_underlying(LogPolicy::OnFailure);
    {
        std::lock_guard<std::mutex> lk(ctx->mtx);
        policy            = ctx->log_policy;
        policy_overridden = ctx->log_policy_overridden;
    }
    if (!policy_overridden) {
        policy = static_cast<LogPolicy>(detail::default_log_policy_storage().load(std::memory_order_acquire));
    }

    buffer.logs.emplace_back(message);
    const auto policy_bits = gentest::to_underlying(policy);
    if ((policy_bits & always_bits) == always_bits) {
        buffer.event_lines.emplace_back(message);
        buffer.event_kinds.push_back('A');
    } else if ((policy_bits & on_failure_bits) != 0) {
        buffer.event_lines.emplace_back(message);
        buffer.event_kinds.push_back('L');
    }
}

void set_log_policy(LogPolicy policy) {
    auto ctx = detail::current_test_storage();
    if (!detail::accepts_late_test_operation(ctx)) {
        return;
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->log_policy            = policy;
    ctx->log_policy_overridden = true;
}

void set_default_log_policy(LogPolicy policy) {
    detail::default_log_policy_storage().store(gentest::to_underlying(policy), std::memory_order_release);
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
    auto ctx = detail::current_test_storage();
    if (!detail::accepts_late_test_operation(ctx)) {
        detail::fail_without_active_context("xfail called");
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->xfail_requested = true;
    if (!reason.empty()) {
        ctx->xfail_reason = std::string(reason);
    }
}

} // namespace gentest
