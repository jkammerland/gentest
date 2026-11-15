#pragma once

#include "a/service.hpp"
#include "b/service.hpp"
#include "gentest/attributes.h"
#include "gentest/runner.h"
#include "public/a_mocks.hpp"
#include "public/b_mocks.hpp"

// Both mock targets name their DEFS file mocks.hpp; each public header must
// reach its own.
[[using gentest: test("explicit_mock_defs_layout/same_name")]]
inline void same_name() {
    using namespace gentest::asserts;

    layout_a::mocks::ServiceMock a;
    gentest::expect(a, &layout_a::Service::value).times(1).returns(1);
    layout_a::Service *a_api = &a;
    EXPECT_EQ(a_api->value(), 1);

    layout_b::mocks::ServiceMock b;
    gentest::expect(b, &layout_b::Service::value).times(1).returns(2);
    layout_b::Service *b_api = &b;
    EXPECT_EQ(b_api->value(), 2);
}
