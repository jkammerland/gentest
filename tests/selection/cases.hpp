#pragma once

#include "gentest/runner.h"

namespace selection_tests {

struct [[gentest::fixture(global)]] Probe : gentest::FixtureSetup {
    void setUp() override;
};

[[using gentest: test("alpha"), fast]] void                 alpha(Probe &);
[[using gentest: test("beta"), slow]] void                  beta();
[[using gentest: test("skipped"), skip, slow]] void         skipped();
[[using gentest: test("fatal"), death, fast]] void          fatal();
[[using gentest: bench("measured"), bench_once, fast]] void measured();
[[using gentest: jitter("latency"), slow]] void             latency();

[[gentest::test("untagged")]] void untagged();

} // namespace selection_tests
