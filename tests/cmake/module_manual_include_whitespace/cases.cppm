module;

#include "public/manual_include_whitespace_mocks.hpp"

export module gentest.manual_include_whitespace;

import gentest;

using namespace gentest::asserts;

export [[using gentest: test("whitespace/manual_spaced_include")]]
void manual_spaced_include() {
    shared::mocks::ServiceMock service;
    gentest::expect(service, &shared::Service::compute).times(1).with(4).returns(9);

    shared::Service &base = service;
    EXPECT_EQ(base.compute(4), 9);
}
