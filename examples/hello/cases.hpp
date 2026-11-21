#pragma once

#include "gentest/attributes.h"

namespace hello {

[[gentest::test]]
void addition();

[[gentest::test]]
void greeting();

} // namespace hello
