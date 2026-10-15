#pragma once

#include "gentest/runner.h"

namespace report_stream {

struct [[gentest::fixture(global)]] Shared : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override;
    void tearDown() override;
};

struct Local : gentest::FixtureSetup, gentest::FixtureTearDown {
    bool fail_teardown = false;
    void setUp() override;
    void tearDown() override;
};

[[using gentest: bench("success"), bench_once]] void success(Local &sample);
[[using gentest: bench("failure"), bench_once]] void failure(Local &sample);
[[using gentest: test("ordinary")]] void             ordinary();

} // namespace report_stream
