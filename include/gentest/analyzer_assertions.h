#pragma once

// Opt-in, include-based fatal assertion macros for flow-sensitive analyzers.
// The ordinary APIs in assertions.h remain macro-free and module-friendly.
#include "gentest/assertions.h"

#if defined(ASSERT_TRUE) || defined(ASSERT_FALSE)
#error "gentest/analyzer_assertions.h requires ASSERT_TRUE and ASSERT_FALSE to be undefined"
#endif

#define ASSERT_TRUE(condition, ...)                                                                                                        \
    do {                                                                                                                                   \
        ::gentest::detail::require_owner_context("assertion/expectation called");                                                          \
        if (!(condition)) {                                                                                                                \
            const auto _gentest_assert_loc = std::source_location::current();                                                              \
            ::gentest::detail::fatal_assertion(                                                                                            \
                "ASSERT_TRUE", ::gentest::detail::failure_text("ASSERT_TRUE", _gentest_assert_loc __VA_OPT__(, __VA_ARGS__)),              \
                _gentest_assert_loc, "gentest::analyzer::ASSERT_TRUE");                                                                    \
        }                                                                                                                                  \
    } while (false)

#define ASSERT_FALSE(condition, ...)                                                                                                       \
    do {                                                                                                                                   \
        ::gentest::detail::require_owner_context("assertion/expectation called");                                                          \
        if (condition) {                                                                                                                   \
            const auto _gentest_assert_loc = std::source_location::current();                                                              \
            ::gentest::detail::fatal_assertion(                                                                                            \
                "ASSERT_FALSE", ::gentest::detail::failure_text("ASSERT_FALSE", _gentest_assert_loc __VA_OPT__(, __VA_ARGS__)),            \
                _gentest_assert_loc, "gentest::analyzer::ASSERT_FALSE");                                                                   \
        }                                                                                                                                  \
    } while (false)
