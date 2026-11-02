#include "runner_debug.h"

#include <csignal>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace gentest::detail {
namespace {
thread_local bool assertion_break_enabled = false;
}

AssertionBreakScope::AssertionBreakScope(bool enabled) noexcept : previous_(assertion_break_enabled) { assertion_break_enabled = enabled; }

AssertionBreakScope::~AssertionBreakScope() { assertion_break_enabled = previous_; }

bool assertion_break_supported() noexcept {
#if defined(_MSC_VER) || defined(SIGTRAP)
    return true;
#else
    return false;
#endif
}

void break_on_assertion_failure() noexcept {
    if (!assertion_break_enabled) {
        return;
    }

    // A debugger may continue past this trap; normal failure recording follows.
#if defined(_MSC_VER)
    __debugbreak();
#elif defined(SIGTRAP)
    (void)std::raise(SIGTRAP);
#endif
}

} // namespace gentest::detail
