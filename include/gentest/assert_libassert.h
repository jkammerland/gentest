#pragma once

// Integration adapter for libassert -> gentest.
//
// Usage in a test TU (your cases.cpp):
//   #include "gentest/assert_libassert.h"  // installs handler
//   [[using gentest: test("suite/name")]]
//   void t() { ASSERT(1 + 1 == 2); /* or other libassert macros */ }
//
// This header installs a libassert failure handler that reports failures into
// gentest's current test context. EXPECT-style checks are recorded as non-fatal
// failures; ASSERT/PANIC/UNREACHABLE-style checks abort the current test by
// throwing gentest::assertion so the runner can continue with the next test.

#include <string>
#include <cstdio>
#include <exception>

#include <gentest/runner.h>

#include <libassert/assert.hpp>

namespace gentest::libassert_integration {

// Non-fatal expectation hint for the current thread, to emulate EXPECT semantics
// while still leveraging libassert's ASSERT decomposition.
inline thread_local int g_expect_nonfatal_depth = 0;

struct NonFatalGuard {
    NonFatalGuard()  { ++g_expect_nonfatal_depth; }
    ~NonFatalGuard() { --g_expect_nonfatal_depth; }
};

inline bool is_nonfatal_scope() { return g_expect_nonfatal_depth > 0; }

template <class F>
inline decltype(auto) with_nonfatal(F&& fn) {
    NonFatalGuard g;
    return fn();
}

// Defer to gentest's global exceptions detection
#ifndef GENTEST_EXCEPTIONS_ENABLED
#  if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#    define GENTEST_EXCEPTIONS_ENABLED 1
#  else
#    define GENTEST_EXCEPTIONS_ENABLED 0
#  endif
#endif

inline void failure_handler(const ::libassert::assertion_info& info) {
    // Prefer libassert's own pretty string; let it decide width/colors.
    std::string message = info.to_string();

    // Record into the active gentest context (includes per-test logs if enabled).
    ::gentest::detail::record_failure(message);

    // Heuristic: treat macros containing "EXPECT" as non-fatal (if present in the
    // toolchain); everything else fatal. This mirrors gtest/catch behavior for
    // EXPECT vs ASSERT without coupling to libassert internals.
    const std::string& macro = info.macro_name;
    const bool is_expect_macro = macro.find("EXPECT") != std::string::npos;
    const bool is_expect_scope = is_nonfatal_scope();

    if (!(is_expect_macro || is_expect_scope)) {
        // Fatal: abort current test. Behavior depends on exception support.
#if GENTEST_EXCEPTIONS_ENABLED
        // Runner catches this and reports [ FAIL ].
        throw ::gentest::assertion("libassert::" + macro);
#else
        // No exceptions available: delegate to gentest's generic termination path.
        ::gentest::detail::terminate_no_exceptions_fatal("libassert");
#endif
    }
}

// Install the handler globally. Call once early in program init, or rely on
// the static initializer below by including this header in your test TU.
inline void install() { ::libassert::set_failure_handler(&failure_handler); }

// Static initializer to auto-install when the header is included in a TU.
struct AutoInstall {
    AutoInstall() { install(); }
};
inline static AutoInstall _auto_install{};

} // namespace gentest::libassert_integration

// Convenience macros providing EXPECT/ASSERT_EQ-style helpers on top of
// libassert's expression-decomposing ASSERT. These mirror typical gtest/catch
// ergonomics without losing libassert's diagnostics.

#ifndef GENTEST_NO_EXPECT_MACROS

#define EXPECT(expr, ...) \
    ::gentest::libassert_integration::with_nonfatal([&] { ASSERT((expr), ##__VA_ARGS__); })

#define EXPECT_EQ(lhs, rhs, ...) \
    ::gentest::libassert_integration::with_nonfatal([&] { ASSERT(((lhs) == (rhs)), ##__VA_ARGS__); })

#define EXPECT_NE(lhs, rhs, ...) \
    ::gentest::libassert_integration::with_nonfatal([&] { ASSERT(((lhs) != (rhs)), ##__VA_ARGS__); })

#endif // GENTEST_NO_EXPECT_MACROS

#ifndef GENTEST_NO_ASSERT_EQ_MACROS

#define ASSERT_EQ(lhs, rhs, ...) ASSERT(((lhs) == (rhs)), ##__VA_ARGS__)
#define ASSERT_NE(lhs, rhs, ...) ASSERT(((lhs) != (rhs)), ##__VA_ARGS__)

#endif // GENTEST_NO_ASSERT_EQ_MACROS
