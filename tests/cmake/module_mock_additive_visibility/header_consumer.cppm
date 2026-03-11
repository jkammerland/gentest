module;

#include "shared_service.hpp"

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#endif

export module gentest.additive_header_consumer;

#if !defined(GENTEST_CODEGEN)
import gentest;
import gentest.mock;
#endif

using namespace gentest::asserts;

[[using gentest: test("additive/header_defined_from_module")]]
void header_defined_from_module() {
    gentest::mock<shared::Service> service;
    gentest::expect(service, &shared::Service::compute).times(1).with(4).returns(9);

#if !defined(GENTEST_CODEGEN)
    shared::Service &base = service;
    EXPECT_EQ(base.compute(4), 9);
#endif
}
