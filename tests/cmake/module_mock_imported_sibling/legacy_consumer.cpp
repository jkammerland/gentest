#if defined(__clang__) && __clang_major__ == 20 && defined(__has_include) && __has_include(<bits/c++config.h>) && !defined(_LIBCPP_VERSION)
#define GENTEST_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN 1
#else
#define GENTEST_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN 0
#endif

#if !GENTEST_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN
import gentest.imported_sibling_mocks;
#endif

#include "gentest/attributes.h"
#include "gentest/runner.h"

using namespace gentest::asserts;

[[using gentest: test("imported_sibling/legacy_cpp_importer")]]
void legacy_cpp_importer() {
#if GENTEST_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN
    gentest::skip("clang 20 + libstdc++ cannot import this mock-bearing provider module into a classic translation unit");
#else
    imported_sibling::mocks::ServiceMock service;
    gentest::expect(service, &imported_sibling::provider::Service::compute).times(1).with(8).returns(21);

    imported_sibling::provider::Service &base = service;
    EXPECT_EQ(base.compute(8), 21);
#endif
}
