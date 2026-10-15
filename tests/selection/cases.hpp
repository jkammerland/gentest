#pragma once

#include "gentest/runner.h"

namespace selection_tests {

struct [[gentest::fixture(global)]] Probe : gentest::FixtureSetup {
    void setUp() override;
};

[[using gentest: test("alpha")]] void                 alpha();
[[using gentest: test("beta")]] void                  beta();
[[using gentest: test("skipped"), skip]] void         skipped();
[[using gentest: test("fatal"), death]] void          fatal();
[[using gentest: bench("measured"), bench_once]] void measured();
[[using gentest: jitter("latency")]] void             latency();

} // namespace selection_tests
