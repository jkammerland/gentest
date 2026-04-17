module;

export module gentest.story034.module_registration;

import gentest;

namespace story034_module_registration {

struct Fixture : gentest::FixtureSetup {
    void setUp() override { value = 7; }
    int value = 0;
};

[[using gentest: test("module_registration/non_exported_fixture")]]
void non_exported_fixture(Fixture& fixture) {
    gentest::asserts::EXPECT_EQ(fixture.value, 7);
}

} // namespace story034_module_registration
