#include "gentest/context.h"
#include "gentest/detail/fixture_runtime.h"

int main() {
    auto  context                  = gentest::get_current_context();
    auto  guard                    = gentest::set_current_context(context);
    auto *setup_shared_fixtures    = &gentest::detail::setup_shared_fixtures;
    auto *teardown_shared_fixtures = &gentest::detail::teardown_shared_fixtures;
    (void)setup_shared_fixtures;
    (void)teardown_shared_fixtures;
    auto *log_fn                  = &gentest::log;
    auto *skip_fn                 = &gentest::skip;
    auto *xfail_fn                = &gentest::xfail;
    auto *add_log_sink_fn         = &gentest::add_log_sink;
    auto *remove_log_sink_fn      = &gentest::remove_log_sink;
    auto *remove_all_log_sinks_fn = &gentest::remove_all_log_sinks;
    (void)add_log_sink_fn;
    (void)remove_log_sink_fn;
    (void)remove_all_log_sinks_fn;
    gentest::skip_if(false, "unused");
    gentest::xfail_if(false, "unused");
    return log_fn == nullptr || skip_fn == nullptr || xfail_fn == nullptr ? 1 : 0;
}
