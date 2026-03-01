#include "gentest/runner.h"

#include <memory>

namespace regressions::shared_setup_skip {

struct [[using gentest: fixture(suite)]] NullSuiteFx {
    static std::unique_ptr<NullSuiteFx> gentest_allocate() { return {}; }
};

struct [[using gentest: fixture(global)]] NullGlobalFx {
    static std::unique_ptr<NullGlobalFx> gentest_allocate() { return {}; }
};

[[using gentest: test("regressions/shared_setup_skip/suite_free")]]
void suite_free(NullSuiteFx &) {}

[[using gentest: test("regressions/shared_setup_skip/global_free")]]
void global_free(NullGlobalFx &) {}

} // namespace regressions::shared_setup_skip
