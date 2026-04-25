module;

#include <cstdlib>
#include <memory>

#if defined(GENTEST_STORY034_MODULE_CONTEXT)
export module gentest.story034.module_registration;
#else
export module gentest.story034.unconfigured;
#endif

import gentest;

namespace story034_module_registration {

struct Fixture : gentest::FixtureSetup {
    void setUp() override { value = 7; }
    int  value = 0;
};

[[using gentest: test("module_registration/non_exported_fixture")]]
void non_exported_fixture(Fixture &fixture) {
    gentest::asserts::EXPECT_EQ(fixture.value, 7);
}

namespace blocked_scope {

struct [[using gentest: fixture(suite)]] NullSuiteFixture {
    static std::unique_ptr<NullSuiteFixture> gentest_allocate() {
        if (std::getenv("GENTEST_STORY034_BLOCKED_FIXTURE") != nullptr) {
            return {};
        }
        return std::make_unique<NullSuiteFixture>();
    }
};

[[using gentest: test("module_registration/blocked_shared_fixture")]]
void blocked_shared_fixture(NullSuiteFixture &) {}

} // namespace blocked_scope

} // namespace story034_module_registration
