#pragma once

#include "gentest/attributes.h"
#include "gentest/runner.h"
#include "public/gen_dep_mocks.hpp"

[[using gentest: test("explicit_mock_defs_layout/generated_dependency")]]
inline void generated_dependency() {
    using namespace gentest::asserts;

    layout_gen_dep::mocks::ApiMock api;
    gentest::expect(api, &layout_gen_dep::Api::value).times(1).returns(5);
    layout_gen_dep::Api *api_ptr = &api;
    EXPECT_EQ(api_ptr->value(), 5);
}
