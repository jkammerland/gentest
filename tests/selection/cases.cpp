#include "cases.hpp"

#include "gentest/bench_util.h"

#include <cstdlib>

namespace selection_tests {

// A preview which accidentally reaches fixture setup cannot pass the strict
// names-only stdout check, even if it never reaches a case body.
void Probe::setUp() { gentest::log("selection fixture setup"); }
void alpha() { gentest::log("alpha executed"); }
void beta() { gentest::log("beta executed"); }
void skipped() { gentest::fail("skipped body must not run"); }
void fatal() { std::abort(); }
void measured() { gentest::doNotOptimizeAway(42); }
void latency() { gentest::doNotOptimizeAway(42); }

} // namespace selection_tests
