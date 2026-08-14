module;

#include <coroutine>
#include <cstdlib>
#include <memory>

#if defined(GENTEST_STORY034_MODULE_CONTEXT)
export module gentest.story034.module_registration;
#else
export module gentest.story034.unconfigured;
#endif

import gentest;

export {

    [[using gentest: test("module_registration/export_block")]]
    void export_block_case() {}
}

export [[using gentest: test("module_registration/explicit_export")]] void explicit_export_case() {}

namespace story034_module_registration_detail {

struct AliasFixture : gentest::FixtureSetup {
    void setUp() override { value = 11; }
    int  value = 0;
};

} // namespace story034_module_registration_detail

export namespace story034_module_registration {

using AliasFixture = story034_module_registration_detail::AliasFixture;

struct Fixture : gentest::FixtureSetup {
    void setUp() override { value = 7; }
    int  value = 0;
};

[[using gentest: test("module_registration/exported_fixture")]]
void exported_fixture(Fixture &fixture) {
    gentest::asserts::EXPECT_EQ(fixture.value, 7);
}

[[using gentest: test("module_registration/exported_redeclaration")]]
void exported_redeclaration(Fixture &fixture);

[[using gentest: test("module_registration/exported_alias_fixture")]]
void exported_alias_fixture(AliasFixture &fixture) {
    gentest::asserts::EXPECT_EQ(fixture.value, 11);
}

[[using gentest: test("module_registration/builtin_fixture")]]
void builtin_fixture(int &fixture) {
    gentest::asserts::EXPECT_EQ(fixture, 0);
}

[[using gentest: test("module_registration/exported_parameters"), parameters(value, 1, 2)]]
void exported_parameters(int value) {
    gentest::asserts::EXPECT_TRUE(value == 1 || value == 2);
}

template <typename T>
[[using gentest: test("module_registration/exported_template"), template(T, int, long)]]
void exported_template() {
    gentest::asserts::EXPECT_TRUE(sizeof(T) >= sizeof(int));
}

[[using gentest: test("module_registration/exported_async")]]
gentest::async_test<void> exported_async() {
    co_return;
}

[[using gentest: bench("module_registration/exported_bench")]]
void exported_bench() {}

[[using gentest: jitter("module_registration/exported_jitter")]]
void exported_jitter() {}

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

namespace shared_scope {

struct [[using gentest: fixture(suite)]] SuiteFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override {
        ++setup_count;
        value = 17;
    }
    void tearDown() override { ++teardown_count; }

    int setup_count    = 0;
    int teardown_count = 0;
    int value          = 0;
};

struct [[using gentest: fixture(global)]] GlobalFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    static std::unique_ptr<GlobalFixture> gentest_allocate() { return std::make_unique<GlobalFixture>(); }

    void setUp() override {
        ++setup_count;
        value = 23;
    }
    void tearDown() override { ++teardown_count; }

    int setup_count    = 0;
    int teardown_count = 0;
    int value          = 0;
};

[[using gentest: test("module_registration/exported_suite_fixture")]]
void exported_suite_fixture(SuiteFixture &fixture) {
    gentest::asserts::EXPECT_EQ(fixture.setup_count, 1);
    gentest::asserts::EXPECT_EQ(fixture.value, 17);
}

[[using gentest: test("module_registration/exported_global_fixture")]]
void exported_global_fixture(GlobalFixture &fixture) {
    gentest::asserts::EXPECT_EQ(fixture.setup_count, 1);
    gentest::asserts::EXPECT_EQ(fixture.value, 23);
}

} // namespace shared_scope

} // namespace story034_module_registration
