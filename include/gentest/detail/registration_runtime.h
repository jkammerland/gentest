#pragma once

#include "gentest/detail/runtime_config.h"

#include <cstddef>
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
    void (*simple_fn)() = nullptr; // optional direct call for generated void free tests
};

namespace detail {

struct GeneratedStringRange {
    const std::string_view *data = nullptr;
    std::size_t             size = 0;
};

struct GeneratedCase {
    std::string_view name;
    void (*fn)(void *);
    void (*simple_fn)();
    std::string_view     file;
    unsigned             line;
    unsigned             flags;
    GeneratedStringRange tags;
    GeneratedStringRange requirements;
    std::string_view     skip_reason;
    std::string_view     fixture;
    FixtureLifetime      fixture_lifetime;
    std::string_view     suite;
};

} // namespace detail

} // namespace gentest

namespace gentest::detail {

// Called by generated sources to register discovered cases. Not intended for
// direct use in normal test code.
GENTEST_RUNTIME_API void register_cases(std::span<const Case> cases);
GENTEST_RUNTIME_API void register_generated_cases(const GeneratedCase *cases, std::size_t count);

} // namespace gentest::detail
