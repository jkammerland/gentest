#pragma once

#include <gentest/runner.h>

namespace header_declaration_registration {

[[using gentest: test("header_declaration/inline")]]
inline void inline_case() {
    gentest::expect(true);
}

} // namespace header_declaration_registration
