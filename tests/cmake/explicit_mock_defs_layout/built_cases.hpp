#pragma once

#include "gentest/attributes.h"
#include "gentest/runner.h"
#include "public/built_mocks.hpp"

[[using gentest: test("explicit_mock_defs_layout/built_defs")]]
inline void built_defs() {
    using namespace gentest::asserts;

    layout_built::mocks::BuiltMock built;
    gentest::expect(built, &layout_built::Built::value).times(1).returns(6);
    layout_built::Built *built_api = &built;
    EXPECT_EQ(built_api->value(), 6);
}
