#pragma once

#include "gentest/attributes.h"

namespace hello {

[[using gentest: test("addition")]]
void addition();

[[using gentest: test("greeting")]]
void greeting();

} // namespace hello
