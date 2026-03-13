module;

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#endif

export /* consumer */ module gentest.imported_sibling_consumer;

import /* provider */ gentest.imported_sibling_provider;

#if !defined(GENTEST_CODEGEN)
import gentest;
import gentest.mock;
#endif

using namespace gentest::asserts;

export namespace imported_sibling {

[[using gentest: test("imported_sibling/module_mock")]]
void module_mock() {
    gentest::mock<imported_sibling::provider::Service> service;
    gentest::expect(service, &imported_sibling::provider::Service::compute).times(1).with(6).returns(15);

#if !defined(GENTEST_CODEGEN)
    imported_sibling::provider::Service &base = service;
    EXPECT_EQ(base.compute(6), 15);
#endif
}

} // namespace imported_sibling
