#if defined(__clang__) && __clang_major__ == 20 && defined(__GLIBCXX__)
#define GENTEST_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN 1
#else
#define GENTEST_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN 0
#endif

#if !GENTEST_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN
import gentest.imported_sibling_provider;
#endif

#if defined(GENTEST_CODEGEN)
#include "gentest/runner.h"
#if !GENTEST_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#endif
#else
#include "gentest/attributes.h"
#include "gentest/runner.h"
#if !GENTEST_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN
#include "gentest/mock.h"
#endif
#endif

using namespace gentest::asserts;

[[using gentest: test("imported_sibling/legacy_cpp_importer")]]
void legacy_cpp_importer() {
#if GENTEST_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN
    gentest::skip("clang 20 + libstdc++ cannot import this mock-bearing provider module into a classic translation unit");
#else
    gentest::mock<imported_sibling::provider::Service> service;
    gentest::expect(service, &imported_sibling::provider::Service::compute).times(1).with(8).returns(21);

#if !defined(GENTEST_CODEGEN)
    imported_sibling::provider::Service &base = service;
    EXPECT_EQ(base.compute(8), 21);
#endif
#endif
}
