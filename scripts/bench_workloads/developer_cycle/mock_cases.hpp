#pragma once

#include "gentest/attributes.h"
#include "production.hpp"

namespace cycle {

[[gentest::test("mock_charge")]]
void mockCharge();

} // namespace cycle
