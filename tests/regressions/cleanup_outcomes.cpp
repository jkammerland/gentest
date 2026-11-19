#include "cleanup_outcomes.hpp"

namespace cleanup_outcomes {

gentest::awaitable<> blocked(Blocked &) {
    gentest::xfail("expected application failure");
    co_return;
}

void assertion(Assertion &) { gentest::skip("missing prerequisite"); }

namespace cancel {

gentest::awaitable<> pending(Early &, Yielding &, Stalled &, Last &) {
    gentest::skip("unavailable");
    co_return;
}

gentest::awaitable<> peer() {
    co_await entered.wait("teardown entered");
    gentest::fail("peer failure");
}

} // namespace cancel
} // namespace cleanup_outcomes
