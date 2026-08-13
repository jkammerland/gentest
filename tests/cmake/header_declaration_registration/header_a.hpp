#pragma once

#include <gentest/runner.h>
#include <memory>

namespace header_declaration_registration {

struct [[using gentest: fixture(suite)]] SharedFixture : gentest::FixtureSetup {
    void setUp() override;
    int  value = 0;
};

struct LocalFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override;
    void tearDown() override;
    int  value = 0;
};

struct [[using gentest: fixture(global)]] GlobalFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    static std::unique_ptr<GlobalFixture> gentest_allocate();
    void                                  setUp() override;
    void                                  tearDown() override;
    int                                   value = 0;
};

[[using gentest: test("header_declaration/a")]]
void case_a(SharedFixture &fixture);

[[using gentest: test("header_declaration/fixture_parity")]]
void fixture_parity(LocalFixture &local, GlobalFixture &global);

} // namespace header_declaration_registration
