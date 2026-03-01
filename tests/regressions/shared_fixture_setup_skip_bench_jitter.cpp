#include "gentest/runner.h"

#include <memory>

namespace regressions::shared_setup_skip_bench_jitter {

struct [[using gentest: fixture(suite)]] NullSuiteFx {
    static std::unique_ptr<NullSuiteFx> gentest_allocate() { return {}; }
};

struct [[using gentest: fixture(global)]] NullGlobalFx {
    static std::unique_ptr<NullGlobalFx> gentest_allocate() { return {}; }
};

[[using gentest: bench("regressions/shared_setup_skip_bench_jitter/suite_bench")]]
void suite_bench(NullSuiteFx &) {}

[[using gentest: jitter("regressions/shared_setup_skip_bench_jitter/global_jitter")]]
void global_jitter(NullGlobalFx &) {}

} // namespace regressions::shared_setup_skip_bench_jitter
