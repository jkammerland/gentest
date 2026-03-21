#include "b.hpp"
#include "gentest/attributes.h"
[[using gentest: test("dep/b")]] void dep_b() { (void)dep_b_value(); }
