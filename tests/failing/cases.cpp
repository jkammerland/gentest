#include "gentest/runner.h"
using namespace gentest::asserts;

#include <memory>
#include <stdexcept>

#include "mocking/types.h"
#include "gentest/mock.h"

namespace failing {

struct NullFreeFixture {
    static std::unique_ptr<NullFreeFixture> gentest_allocate() { return {}; }
};

[[using gentest: test("alloc/free_null")]]
void free_null_fixture(NullFreeFixture&) {}

struct [[using gentest: fixture(suite)]] NullSuiteFixture {
    static std::unique_ptr<NullSuiteFixture> gentest_allocate() { return {}; }
    [[using gentest: test("alloc/suite_null")]] void t() {}
};

struct [[using gentest: fixture(global)]] NullGlobalFixture {
    static std::unique_ptr<NullGlobalFixture> gentest_allocate() { return {}; }
    [[using gentest: test("alloc/global_null")]] void t() {}
};

[[using gentest: test("single")]]
void will_fail() {
    using namespace gentest::asserts;
    EXPECT_TRUE(false, "non-fatal 1");
    EXPECT_EQ(1, 2, "non-fatal 2");
    ASSERT_TRUE(false, "fatal now");
}

[[using gentest: test("mocking/predicate_mismatch")]]
void predicate_mismatch() {
    using namespace gentest::match;
    gentest::mock<mocking::Ticker> mock_obj;
    gentest::expect(mock_obj, &mocking::Ticker::tick).where_args(Eq(3)).times(1);
    // Mismatch: should record a failure due to predicate not matching
    mock_obj.tick(4);
}

[[using gentest: test("logging/attachment")]]
void logging_attachment() {
    gentest::log_on_fail(true);
    gentest::log("hello from log");
    gentest::log("world from log");
    using namespace gentest::asserts;
    EXPECT_TRUE(false, "trigger failure to capture logs");
}

[[using gentest: test("exceptions/expect_throw_location")]]
void expect_throw_location() {
    EXPECT_THROW((void)0, std::runtime_error);
}

[[using gentest: test("exceptions/expect_no_throw_unknown")]]
void expect_no_throw_unknown() {
    EXPECT_NO_THROW(throw 123);
}

} // namespace failing
