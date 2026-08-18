#pragma once

// A suite authored entirely in a header: the annotated declarations are also
// their own definitions, so the target needs no authored .cpp of its own.

#include "gentest/runner.h"

namespace downstream {

[[using gentest: test("downstream/xrepo/header_only")]]
inline void downstream_header_only() {
    gentest::expect_true(2 + 2 == 4, "inline header case runs");
}

} // namespace downstream
