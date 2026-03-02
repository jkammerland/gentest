#include "gentest/attributes.h"
#include "gentest/fixture.h"

#include <memory>
#include <string_view>

namespace smoke::namespaced {

struct [[gentest::fixture(suite)]] SuiteFixture : gentest::FixtureSetup {
    inline static int set_up_calls = 0;
    int               touched      = 0;
    void              setUp() override { ++set_up_calls; }
};

struct [[gentest::fixture(global)]] GlobalFixture : gentest::FixtureSetup {
    inline static int set_up_calls = 0;
    int               touched      = 0;
    void              setUp() override { ++set_up_calls; }
};

[[gentest::test("smoke/namespaced/first"), gentest::fast, gentest::owner("team-codegen"), gentest::req("REQ-NS-1")]]
void namespaced_first(SuiteFixture *suite_fx, GlobalFixture *global_fx) {
    ++suite_fx->touched;
    ++global_fx->touched;
}

[[gentest::test("smoke/namespaced/second"), gentest::slow, gentest::owner("team-codegen"), gentest::req("REQ-NS-2")]]
void namespaced_second(SuiteFixture &suite_fx, std::shared_ptr<GlobalFixture> global_fx) {
    ++suite_fx.touched;
    ++global_fx->touched;
}

[[maybe_unused, gentest::test("smoke/namespaced/mixed/std_first"), gentest::fast]]
void namespaced_mixed_standard_first() {}

[[clang::annotate("smoke-mixed-scoped"), gentest::test("smoke/namespaced/mixed/scoped_first"), gentest::slow]]
void namespaced_mixed_scoped_first() {}

} // namespace smoke::namespaced
