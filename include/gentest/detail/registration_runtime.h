#pragma once

// Unstable generated-code registration contract. The public runner API sees the
// `Case` model through `case_api.h`, but mutable registration stays here.

#include "gentest/detail/case_api.h"

#include <span>

namespace gentest::detail {

// Called by generated sources to register discovered cases. Not intended for
// direct use in normal test code.
GENTEST_RUNTIME_API void register_cases(std::span<const Case> cases);

// Generated wrappers call this immediately before invoking the authored body.
GENTEST_RUNTIME_API void notify_case_body_start();
GENTEST_RUNTIME_API void notify_case_body_finish() noexcept;

struct CaseBodyScope {
    bool active;
    explicit CaseBodyScope(bool enabled = true) : active(enabled) {
        if (active)
            notify_case_body_start();
    }
    ~CaseBodyScope() {
        if (active)
            notify_case_body_finish();
    }
    CaseBodyScope(const CaseBodyScope &)            = delete;
    CaseBodyScope &operator=(const CaseBodyScope &) = delete;
};

} // namespace gentest::detail
