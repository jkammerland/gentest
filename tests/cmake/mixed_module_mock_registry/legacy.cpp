#include "legacy_service.hpp"

#include "gentest/mock.h"

using namespace gentest::asserts;

[[using gentest: test("mixed/legacy_mock")]]
void legacy_case() {
    gentest::mock<legacy::Service> service;
    gentest::expect(service, &legacy::Service::compute).times(1).with(4).returns(6);

#if !defined(GENTEST_CODEGEN)
    legacy::Service &base = service;
    EXPECT_EQ(base.compute(4), 6);
#endif
}
