#pragma once

#include "gentest/detail/fixture_lifetime.h"

#include <span>
#include <string_view>

namespace gentest {

// Runtime-visible test case description used by manual registration and by
// registry snapshots. Keep the aggregate field order stable for handwritten
// designated initializers; padding is not worth churn across that surface.
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

} // namespace gentest
