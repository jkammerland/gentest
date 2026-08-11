#include "header_a.hpp"

namespace header_declaration_registration {

void SharedFixture::setUp() {
    value = 42;
}

void case_a(SharedFixture &fixture) {
    gentest::expect_eq(fixture.value, 42);
}

} // namespace header_declaration_registration
