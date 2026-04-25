#pragma once

#include "gentest/detail/runtime_base.h"
#include "gentest/log_policy.h"

#include <memory>
#include <source_location>
#include <string_view>
#include <utility>

namespace gentest {

namespace detail {
struct TestContextInfo;
}

// Public context adoption API for multi-threaded/coroutine tests.
using CurrentContext = std::shared_ptr<detail::TestContextInfo>;
using CurrentToken   = CurrentContext;

struct Adoption;

[[nodiscard]] GENTEST_RUNTIME_API auto get_current_context() -> CurrentContext;
[[nodiscard]] GENTEST_RUNTIME_API auto set_current_context(CurrentContext context) -> Adoption;
[[nodiscard]] GENTEST_RUNTIME_API auto get_current_token() -> CurrentToken;
[[nodiscard]] GENTEST_RUNTIME_API auto set_current_token(CurrentToken context) -> Adoption;

struct [[nodiscard]] Adoption {
    Adoption(const Adoption &)            = delete;
    Adoption &operator=(const Adoption &) = delete;
    Adoption(Adoption &&)                 = delete;
    Adoption &operator=(Adoption &&)      = delete;

    GENTEST_RUNTIME_API ~Adoption();

  private:
    friend auto set_current_context(CurrentContext context) -> Adoption;

    GENTEST_RUNTIME_API explicit Adoption(CurrentContext context);

    CurrentContext previous_{};
    CurrentContext adopted_{};
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
