import gentest.imported_sibling_provider;

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#else
import gentest;
import gentest.mock;
#endif

using namespace gentest::asserts;

[[using gentest: test("imported_sibling/legacy_cpp_importer")]]
void legacy_cpp_importer() {
    gentest::mock<imported_sibling::provider::Service> service;
    gentest::expect(service, &imported_sibling::provider::Service::compute).times(1).with(8).returns(21);

#if !defined(GENTEST_CODEGEN)
    imported_sibling::provider::Service &base = service;
    EXPECT_EQ(base.compute(8), 21);
#endif
}
