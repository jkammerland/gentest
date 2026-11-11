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

// Nice mode only relaxes methods without expectations; once a method has
// expectations, calls beyond them must fail exactly as in strict mode.
// Use constant method identities because Ticker also has tadd<int> with the same
// signature, making runtime member-pointer selection ambiguous on the MSVC ABI.
void nice_exhausted_excess_call() {
    gentest::mock<mocking::Ticker> mock_obj;
    gentest::make_nice(mock_obj, true);
    gentest::expect<&mocking::Ticker::tick>(mock_obj, "::mocking::Ticker::tick").times(1);
    mock_obj.tick(1);
    mock_obj.tick(2); // exceeds times(1): must fail
}

void nice_times_zero_call() {
    gentest::mock<mocking::Ticker> mock_obj;
    gentest::make_nice(mock_obj, true);
    gentest::expect<&mocking::Ticker::tick>(mock_obj, "::mocking::Ticker::tick").times(0);
    mock_obj.tick(1); // forbidden by times(0): must fail
}

// Template members configured through a runtime member pointer are matched via
// the dispatcher's fallback identity; exhausting them must fail too.
void nice_exhausted_fallback_identity() {
    gentest::mock<mocking::Ticker> mock_obj;
    gentest::make_nice(mock_obj, true);
    gentest::expect(mock_obj, &mocking::Ticker::tadd<int>).times(1);
    mock_obj.tadd(1);
    mock_obj.tadd(2); // exceeds times(1): must fail
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

// NOLINTNEXTLINE(bugprone-std-exception-baseclass): regression deliberately exercises non-standard exceptions.
void expect_no_throw_unknown() { EXPECT_NO_THROW(throw 123); }

} // namespace failing

namespace failing {

void assert_throw_location() { ASSERT_THROW((void)0, std::runtime_error); }

} // namespace failing

namespace failing {

// NOLINTNEXTLINE(bugprone-std-exception-baseclass): regression deliberately exercises non-standard exceptions.
void assert_no_throw_unknown() { ASSERT_NO_THROW(throw 123); }

} // namespace failing

namespace failing {

void expect_eq_message_values() { EXPECT_EQ(1, 2, "comparison detail"); }

void null_function_pointer() {
    void (*const callback)() = nullptr;
    ASSERT_NE(callback, nullptr, "null function pointer diagnostic");
}

void nonnull_function_pointer() {
    void (*const callback)() = &null_function_pointer;
    void (*const absent)()   = nullptr;
    ASSERT_EQ(callback, absent, "nonnull function pointer diagnostic");
}

} // namespace failing
