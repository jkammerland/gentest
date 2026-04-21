#pragma once

#include "gentest/detail/runtime_config.h"

#include <span>
#include <string_view>

namespace gentest {

// Runtime-visible test case description used by generated code and by runtime
// registry snapshots.
enum class FixtureLifetime {
    None,
    MemberEphemeral,
    MemberSuite,
    MemberGlobal,
};

// Keep the public aggregate field order stable for generated/manual designated
// initializers; the padding check is not worth the churn across that surface.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
struct Case {
    std::string_view name;
    void (*fn)(void *);
    std::string_view                  file;
    unsigned                          line;
    bool                              is_benchmark{false};
    bool                              is_jitter{false};
    bool                              is_baseline{false};
    std::span<const std::string_view> tags;
    std::span<const std::string_view> requirements;
    std::string_view                  skip_reason;
    bool                              should_skip;
    std::string_view                  fixture; // empty for free tests
    FixtureLifetime                   fixture_lifetime;
    std::string_view                  suite;
};

} // namespace gentest

namespace gentest::detail {

// Called by generated sources to register discovered cases. Not intended for
// direct use in normal test code.
GENTEST_RUNTIME_API void register_cases(std::span<const Case> cases);

} // namespace gentest::detail
