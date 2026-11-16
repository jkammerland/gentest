#pragma once

#include <gentest/runner.h>

// A header whose name has more than one dot is still a header: codegen must
// classify it by its last extension and register its case.
namespace header_declaration_registration::dotted {

[[using gentest: test("header_only/dotted")]]
inline void dotted_case() {
    gentest::expect(true);
}

} // namespace header_declaration_registration::dotted
