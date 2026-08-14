#pragma once

#include <gentest/runner.h>

namespace header_declaration_registration {

[[using gentest: test("header_declaration/fallback")]]
inline void fallback_case() {
    gentest::expect(true);
}

} // namespace header_declaration_registration
