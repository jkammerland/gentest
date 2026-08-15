#pragma once

#include "gentest/runner.h"

namespace skiponly {

[[using gentest: test("alpha"), skip("not ready")]]
void alpha();

[[using gentest: test("beta"), skip("flaky")]]
void beta();

} // namespace skiponly
