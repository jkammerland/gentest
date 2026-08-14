#include "contextual_cases.hpp"
#include "header_a.hpp"
#include "macro_cases.hpp"
#include "redeclaration_a.hpp"
#include "same_a/shared_name.hpp"

namespace header_declaration_registration {

struct source_only_helper {
    int value = 0;
};

void SharedFixture::setUp() { value = 42; }

void LocalFixture::setUp() { value = 7; }

void LocalFixture::tearDown() { value = 0; }

std::unique_ptr<GlobalFixture> GlobalFixture::gentest_allocate() { return std::make_unique<GlobalFixture>(); }

void GlobalFixture::setUp() { value = 9; }

void GlobalFixture::tearDown() { value = 0; }

void case_a(SharedFixture &fixture) { gentest::expect_eq(fixture.value, 42); }

void fixture_parity(LocalFixture &local, GlobalFixture &global) {
    gentest::expect_eq(local.value, 7);
    gentest::expect_eq(global.value, 9);
}

void redeclared_case() { gentest::expect(true); }

void macro_case_one() { gentest::expect(true); }

void macro_case_two() { gentest::expect(true); }

void macro_pair_one() { gentest::expect(true); }

void macro_pair_two() { gentest::expect(true); }

void contextual_case_a() { gentest::expect(true); }

void same_basename_a() { gentest::expect(true); }

} // namespace header_declaration_registration
