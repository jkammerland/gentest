#pragma once

#include "gentest/async.h"
#include "gentest/detail/runtime_config.h"

#include <cstdint>
#include <span>
#include <string_view>

// These stay as macros because generated registration code checks them with
// preprocessor conditionals before using newer Case fields.
// NOLINTBEGIN(modernize-macro-to-enum)
#define GENTEST_CASE_API_VERSION            2
#define GENTEST_CASE_API_HAS_ITEMS_PER_CALL 1
// NOLINTEND(modernize-macro-to-enum)

namespace gentest {

// Runtime-visible test case description used by generated code and by runtime
// registry snapshots. This is a source contract, not a stable binary ABI:
// rebuild generated/manual registrations with the runtime they link against.
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
    gentest::detail::AsyncCaseFn      async_fn{nullptr};
    bool                              is_async{false};
    std::uint64_t                     items_per_call{1};
};

} // namespace gentest
