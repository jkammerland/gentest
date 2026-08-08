#include "gentest/runner.h"

namespace smoke::narrow_preamble {

[[using gentest: bench("narrow/full/bench")]]
void benchmark_case() {}

[[using gentest: jitter("narrow/full/jitter")]]
void jitter_case() {}

} // namespace smoke::narrow_preamble
