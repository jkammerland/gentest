#include "local_fixture_teardown_on_throw.hpp"

namespace regressions::local_teardown {

void throwing_case(BodySkipFx &) {
    // Triggers exception-based early exit from the test body.
    gentest::skip("intentional skip to exercise unwinding");
}

void setup_throw_case(SetupThrowProbeFx &, SetupThrowFx &) {}
void setup_skip_case(SetupSkipProbeFx &, SetupSkipFx &) {}

} // namespace regressions::local_teardown
