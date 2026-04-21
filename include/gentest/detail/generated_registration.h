#pragma once

#include "gentest/detail/fixture_lifetime.h"
#include "gentest/detail/runtime_config.h"

#include <cstddef>
#include <string_view>

namespace gentest::detail {

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

GENTEST_RUNTIME_API void register_generated_cases(const GeneratedCase *cases, std::size_t count);

} // namespace gentest::detail
