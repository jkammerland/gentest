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

template <typename T> struct AliasTemplateFixture : gentest::FixtureSetup {
    void setUp() override { value = T{13}; }
    T    value{};
};

} // namespace story034_module_registration_detail

namespace story034_module_registration {

struct [[using gentest: fixture(suite)]] HiddenSharedFixture : gentest::FixtureSetup {
    void setUp() override { value = 19; }
    int  value = 0;
};

} // namespace story034_module_registration

export namespace story034_module_registration {

using AliasFixture                               = story034_module_registration_detail::AliasFixture;
template <typename T> using AliasTemplateFixture = story034_module_registration_detail::AliasTemplateFixture<T>;
using SharedFixtureAlias                         = HiddenSharedFixture;
using ExportedTemplateArgument                   = story034_module_registration_detail::AliasFixture;

enum class EnumFixture { Default };

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

[[using gentest: test("module_registration/exported_alias_template_fixture")]]
void exported_alias_template_fixture(AliasTemplateFixture<int> &fixture) {
    gentest::asserts::EXPECT_EQ(fixture.value, 13);
}

[[using gentest: test("module_registration/exported_enum_fixture")]]
void exported_enum_fixture(EnumFixture &fixture) {
    gentest::asserts::EXPECT_EQ(fixture, EnumFixture::Default);
}

[[using gentest: test("module_registration/exported_shared_fixture_alias")]]
void exported_shared_fixture_alias(SharedFixtureAlias &fixture) {
    gentest::asserts::EXPECT_EQ(fixture.value, 19);
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

template <typename T>
[[using gentest: test("module_registration/exported_template_alias_binding"), template(T, ExportedTemplateArgument)]]
void exported_template_alias_binding() {
    gentest::asserts::EXPECT_TRUE(sizeof(T) > 0);
}

[[using gentest: test("module_registration/exported_async")]]
gentest::async_test<void> exported_async();

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
