#include "header_b.hpp"

namespace header_declaration_registration {

void case_b(SharedFixture &fixture) {
    gentest::expect_eq(fixture.value, 42);
}

} // namespace header_declaration_registration
