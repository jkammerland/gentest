#pragma once

#include <gentest/runner.h>

namespace header_declaration_registration {

struct [[using gentest: fixture(suite)]] SharedFixture : gentest::FixtureSetup {
    void setUp() override;
    int value = 0;
};

[[using gentest: test("header_declaration/a")]]
void case_a(SharedFixture &fixture);

} // namespace header_declaration_registration
