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
namespace ctx {

using Token = std::shared_ptr<detail::TestContextInfo>;

GENTEST_RUNTIME_API auto current() -> Token;

struct Adopt {
    Adopt(const Adopt &)            = delete;
    Adopt &operator=(const Adopt &) = delete;
    Adopt(Adopt &&)                 = delete;
    Adopt &operator=(Adopt &&)      = delete;

    GENTEST_RUNTIME_API explicit Adopt(Token t);
    GENTEST_RUNTIME_API ~Adopt();

    Token prev{};
    Token adopted{};
};

} // namespace ctx

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
