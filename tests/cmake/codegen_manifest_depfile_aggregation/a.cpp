#include "a.hpp"
#include "gentest/attributes.h"
[[using gentest: test("dep/a")]] void dep_a() { (void)dep_a_value(); }
