#pragma once

#include "gentest/detail/runtime_base.h"
#include "gentest/log_policy.h"

#include <memory>
#include <source_location>
#include <stop_token>
#include <string_view>
#include <utility>

namespace gentest {

class CurrentContext;

namespace detail {
struct CurrentContextAccess;
struct TestContextInfo;
} // namespace detail

// Public context lease API for multi-threaded/coroutine tests.
class CurrentContext {
  public:
    CurrentContext() noexcept = default;

    [[nodiscard]] explicit                 operator bool() const noexcept { return static_cast<bool>(ctx_); }
    [[nodiscard]] GENTEST_RUNTIME_API auto stop_token() const noexcept -> std::stop_token;
    [[nodiscard]] GENTEST_RUNTIME_API auto stop_requested() const noexcept -> bool;

  private:
    friend struct detail::CurrentContextAccess;
    friend auto get_current_context() -> CurrentContext;

    explicit CurrentContext(std::shared_ptr<detail::TestContextInfo> ctx) noexcept : ctx_(std::move(ctx)) {}

    std::shared_ptr<detail::TestContextInfo> ctx_{};
};

using CurrentToken = CurrentContext;

struct CurrentContextLease;

[[nodiscard]] GENTEST_RUNTIME_API auto get_current_context() -> CurrentContext;
[[nodiscard]] GENTEST_RUNTIME_API auto set_current_context(CurrentContext context) -> CurrentContextLease;
[[nodiscard]] GENTEST_RUNTIME_API auto get_current_token() -> CurrentToken;
[[nodiscard]] GENTEST_RUNTIME_API auto set_current_token(CurrentToken context) -> CurrentContextLease;

struct [[nodiscard]] CurrentContextLease {
    CurrentContextLease(const CurrentContextLease &)            = delete;
    CurrentContextLease &operator=(const CurrentContextLease &) = delete;
    CurrentContextLease(CurrentContextLease &&)                 = delete;
    CurrentContextLease &operator=(CurrentContextLease &&)      = delete;

    GENTEST_RUNTIME_API ~CurrentContextLease();

  private:
    friend auto set_current_context(CurrentContext context) -> CurrentContextLease;

    GENTEST_RUNTIME_API explicit CurrentContextLease(CurrentContext context);

    CurrentContext previous_{};
    CurrentContext leased_{};
};

// Lightweight per-test logging.
// - `set_log_policy()` overrides log visibility for the active test context.
// - `set_default_log_policy()` controls the process-global default when a test
//   does not override it explicitly.
GENTEST_RUNTIME_API void log(std::string_view message);

GENTEST_RUNTIME_API void set_log_policy(LogPolicy policy);
GENTEST_RUNTIME_API void set_default_log_policy(LogPolicy policy);

[[noreturn]] GENTEST_RUNTIME_API void skip(std::string_view reason = {}, const std::source_location &loc = std::source_location::current());

inline void skip_if(bool condition, std::string_view reason = {}, const std::source_location &loc = std::source_location::current()) {
    if (condition) {
        skip(reason, loc);
    }
}

GENTEST_RUNTIME_API void xfail(std::string_view reason = {}, const std::source_location &loc = std::source_location::current());

inline void xfail_if(bool condition, std::string_view reason = {}, const std::source_location &loc = std::source_location::current()) {
    if (condition) {
        xfail(reason, loc);
    }
}

} // namespace gentest
