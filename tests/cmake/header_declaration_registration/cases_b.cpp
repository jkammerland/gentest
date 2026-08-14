#include "contextual_cases.hpp"
#include "header_b.hpp"
#include "inline_cases.hpp"
#include "redeclaration_b.hpp"
#include "same_b/shared_name.hpp"

namespace header_declaration_registration {

void case_b(SharedFixture &fixture) { gentest::expect_eq(fixture.value, 42); }

void contextual_case_b() { gentest::expect(true); }

void same_basename_b() { gentest::expect(true); }

} // namespace header_declaration_registration
