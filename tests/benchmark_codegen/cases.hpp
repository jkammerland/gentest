#pragma once

#include "gentest/attributes.h"

[[using gentest: bench("codegen/once_supplied"), bench_once, bench_warmup(0), bench_time("supplied"), fast, owner("codegen")]]
void once_supplied();

[[using gentest: benchmark("codegen/once_runner"), bench_once, bench_time("runner")]]
void once_runner();

[[using gentest: bench_warmup(7), bench("codegen/calibrated_warmup")]]
void calibrated_warmup();

[[using gentest: bench("codegen/default")]]
void default_benchmark();

[[using gentest: jitter("codegen/jitter")]]
void default_jitter();

[[using gentest: test("codegen/test"), slow]]
void ordinary_test();
