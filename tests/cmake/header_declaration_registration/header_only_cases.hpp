#pragma once

#include <gentest/runner.h>

namespace header_declaration_registration::header_only {

[[using gentest: test("header_only/first")]]
inline void first_case() {
    gentest::expect(true);
}

[[using gentest: test("header_only/second")]]
inline void second_case() {
    gentest::expect(2 + 2 == 4);
}

} // namespace header_declaration_registration::header_only
