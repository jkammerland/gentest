#pragma once

#include "gentest/test.h"

[[gentest::test("widget/always")]] inline void always() {}

#if __has_include(<widget/config.hpp>)
#include <widget/config.hpp>

[[gentest::test("widget/optional")]] inline void optional_feature() { gentest::expect_eq(widget_size, 5); }
#endif
