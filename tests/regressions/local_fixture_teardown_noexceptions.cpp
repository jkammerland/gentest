#include "local_fixture_teardown_noexceptions.hpp"

using namespace gentest::asserts;

namespace regressions::local_teardown_noexceptions {

void fatal_assert(LocalFx &, gentest::TemporaryDirectory &directory) {
    // Confirm setup ran before the process aborts and leaves cleanup to the hook.
    (void)std::fputs(directory.path().empty() ? "empty temporary directory path\n" : "temporary directory ready\n", stderr);
    ASSERT_TRUE(false, "intentional fatal assert in no-exception mode");
}

} // namespace regressions::local_teardown_noexceptions

namespace noexceptions {

void fatal_require() { gentest::require(false, "intentional fatal requirement"); }

void fatal_fail() { gentest::fail("intentional fatal failure"); }

void earlier_expectation() {
    gentest::expect(false, "independent failure before termination");
    fatal_require();
}

} // namespace noexceptions
