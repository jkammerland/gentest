module;

#if __has_include(<widget/config.hpp>)
#include <widget/config.hpp>
#endif

export module downstream.network;

import gentest;

export [[gentest::test("widget/always")]] void always() {}

#if __has_include(<widget/config.hpp>)
export [[gentest::test("widget/optional")]] void optional_feature() { gentest::expect_eq(widget_size, 5); }
#endif
