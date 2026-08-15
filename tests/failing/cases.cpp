#include "cases.hpp"

namespace failing {

void free_null_fixture(NullFreeFixture &) {}

} // namespace failing

namespace failing {

void will_fail() {
    using namespace gentest::asserts;
    EXPECT_TRUE(false, "non-fatal 1");
    EXPECT_EQ(1, 2, "non-fatal 2");
    ASSERT_TRUE(false, "fatal now");
}

} // namespace failing

namespace failing {

void predicate_mismatch() {
    using namespace gentest::match;
    gentest::mock<mocking::Ticker> mock_obj;
    gentest::expect<&mocking::Ticker::tick>(mock_obj, "::mocking::Ticker::tick").where_args(Eq(3)).times(1);
    // Mismatch: should record a failure due to predicate not matching
    mock_obj.tick(4);
}

} // namespace failing

namespace failing {

void ambiguous_template_member_pointer() {
    gentest::mock<mocking::Ticker> mock_obj;
    gentest::expect(mock_obj, &mocking::Ticker::tadd<int>).times(1);
}

} // namespace failing

namespace failing {

void logging_attachment() {
    gentest::log("hello from log");
    gentest::log("world from log");
    using namespace gentest::asserts;
    EXPECT_TRUE(false, "trigger failure to capture logs");
}

} // namespace failing

namespace failing {

void expect_throw_location() { EXPECT_THROW((void)0, std::runtime_error); }

} // namespace failing

namespace failing {

void expect_no_throw_unknown() { EXPECT_NO_THROW(throw 123); }

} // namespace failing

namespace failing {

void assert_throw_location() { ASSERT_THROW((void)0, std::runtime_error); }

} // namespace failing

namespace failing {

void assert_no_throw_unknown() { ASSERT_NO_THROW(throw 123); }

} // namespace failing

namespace failing {

void expect_eq_message_values() { EXPECT_EQ(1, 2, "comparison detail"); }

} // namespace failing
