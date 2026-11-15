#pragma once

#include "gentest/attributes.h"
#include "gentest/runner.h"
#include "public/split_mocks.hpp"

// One DEFS file is authored in the source tree, the other is generated into the
// build tree.
[[using gentest: test("explicit_mock_defs_layout/split")]]
inline void split() {
    using namespace gentest::asserts;

    layout_split::mocks::AuthoredMock authored;
    gentest::expect(authored, &layout_split::Authored::value).times(1).returns(3);
    layout_split::Authored *authored_api = &authored;
    EXPECT_EQ(authored_api->value(), 3);

    layout_split::mocks::GeneratedMock generated;
    gentest::expect(generated, &layout_split::Generated::value).times(1).returns(4);
    layout_split::Generated *generated_api = &generated;
    EXPECT_EQ(generated_api->value(), 4);
}
