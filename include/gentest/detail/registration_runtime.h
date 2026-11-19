#pragma once

// Unstable generated-code registration contract. The public runner API sees the
// `Case` model through `case_api.h`, but mutable registration stays here.

#include "gentest/detail/case_api.h"

#include <span>
#include <type_traits>

namespace gentest::detail {

// Called by generated sources to register discovered cases. Not intended for
// direct use in normal test code.
GENTEST_RUNTIME_API void register_cases(std::span<const Case> cases);

// Generated wrappers call this immediately before invoking the authored body.
GENTEST_RUNTIME_API void notify_case_body_start();
GENTEST_RUNTIME_API void notify_case_body_finish() noexcept;

// Passing a staged value to the authored function must not run user code.
template <class... T>
constexpr bool safe_death_parameters =
    ((std::is_reference_v<T> || (std::is_trivially_constructible_v<T, T &&> && std::is_trivially_destructible_v<T>)) && ...);

struct CaseBodyScope {
    bool active;
    explicit CaseBodyScope(bool enabled = true) : active(enabled) {
        if (active)
            notify_case_body_start();
    }
    ~CaseBodyScope() { finish(); }
    void finish() noexcept {
        if (active) {
            notify_case_body_finish();
            active = false;
        }
    }
    CaseBodyScope(const CaseBodyScope &)            = delete;
    CaseBodyScope &operator=(const CaseBodyScope &) = delete;
};

} // namespace gentest::detail
