#pragma once

// A suite authored entirely in a header: the annotated declarations are also
// their own definitions, so the target needs no authored .cpp of its own.

#include "gentest/runner.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace downstream {

[[using gentest: test("downstream/xrepo/header_only")]]
inline void downstream_header_only() {
    gentest::expect_true(2 + 2 == 4, "inline header case runs");
}

[[using gentest: test("death/exit"), death]]
inline void fatal_input() {
    const char *setting = std::getenv("GENTEST_XREPO_DEATH_FIXTURE");
    if (!setting || std::string_view(setting) != "installed suite")
        return;
    std::fputs("fatal input 42\n", stderr);
    std::fflush(stderr);
    std::_Exit(23);
}

[[using gentest: test("death/skip"), death]]
inline void skipped_input() {
    gentest::skip("optional input unavailable");
}

} // namespace downstream
