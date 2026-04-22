#include "gentest/context.h"
#include "gentest/detail/fixture_runtime.h"

int main() {
    auto                tok = gentest::ctx::current();
    gentest::ctx::Adopt guard(tok);
    auto               *setup_shared_fixtures    = &gentest::detail::setup_shared_fixtures;
    auto               *teardown_shared_fixtures = &gentest::detail::teardown_shared_fixtures;
    (void)setup_shared_fixtures;
    (void)teardown_shared_fixtures;
    gentest::log("shared-runtime-consumer");
    gentest::set_log_policy(gentest::LogPolicy::Never);
    gentest::skip_if(false, "unused");
    gentest::xfail_if(false, "unused");
    return 0;
}
