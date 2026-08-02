#include "gentest/assertions.h"
#include "gentest/detail/runtime_support.h"

namespace runtime_reporting_regressions {

void measured_bench_github_annotation_failure(void *) {
    if (gentest::detail::bench_phase() != gentest::detail::BenchPhase::Call) {
        return;
    }
    gentest::asserts::EXPECT_TRUE(false, "measured benchmark annotation source marker");
}

void measured_jitter_github_annotation_failure(void *) {
    if (gentest::detail::bench_phase() != gentest::detail::BenchPhase::Call) {
        return;
    }
    gentest::asserts::EXPECT_TRUE(false, "measured jitter annotation source marker");
}

} // namespace runtime_reporting_regressions
