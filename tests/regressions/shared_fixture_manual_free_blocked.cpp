#include "gentest/runner.h"

#include <memory>

namespace regressions::manual_free_blocked {

struct [[using gentest: fixture(suite)]] CreateSkipFx {
    static std::unique_ptr<CreateSkipFx> gentest_allocate() {
        gentest::skip("manual-free-create-skip");
        return {};
    }
};

struct [[using gentest: fixture(suite)]] CreateAssertFx {
    static std::unique_ptr<CreateAssertFx> gentest_allocate() {
        gentest::asserts::ASSERT_TRUE(false, "manual-free-create-assert");
        return {};
    }
};

struct [[using gentest: fixture(suite)]] SetupSkipFx : gentest::FixtureSetup {
    void setUp() override { gentest::skip("manual-free-setup-skip"); }
};

struct [[using gentest: fixture(suite)]] SetupAssertFx : gentest::FixtureSetup {
    void setUp() override { gentest::asserts::ASSERT_TRUE(false, "manual-free-setup-assert"); }
};

[[using gentest: test("regressions/manual_free_blocked/create_skip")]]
void create_skip(CreateSkipFx &) {}

[[using gentest: test("regressions/manual_free_blocked/create_assert")]]
void create_assert(CreateAssertFx &) {}

[[using gentest: test("regressions/manual_free_blocked/setup_skip")]]
void setup_skip(SetupSkipFx &) {}

[[using gentest: test("regressions/manual_free_blocked/setup_assert")]]
void setup_assert(SetupAssertFx &) {}

} // namespace regressions::manual_free_blocked
