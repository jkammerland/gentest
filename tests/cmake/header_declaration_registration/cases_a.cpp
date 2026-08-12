#include "header_a.hpp"

namespace header_declaration_registration {

struct source_only_helper {
    int value = 0;
};

void SharedFixture::setUp() {
    value = 42;
}

void case_a(SharedFixture &fixture) {
    gentest::expect_eq(fixture.value, 42);
}

} // namespace header_declaration_registration
