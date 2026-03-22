#if defined(__clang__) && __clang_major__ == 20 && defined(__has_include) && __has_include(<bits/c++config.h>) && !defined(_LIBCPP_VERSION)
#define GENTEST_MULTI_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN 1
#define GENTEST_MULTI_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN_REASON                                                \
    "clang 20 + libstdc++ cannot import these mock-bearing provider modules into a classic translation unit"
#else
#define GENTEST_MULTI_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN 0
#define GENTEST_MULTI_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN_REASON ""
#endif

#if !GENTEST_MULTI_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN
import gentest.multi_imported_sibling_provider_alpha;
import gentest.multi_imported_sibling_provider_beta;
#endif

#if defined(GENTEST_CODEGEN)
#include "gentest/runner.h"
#if !GENTEST_MULTI_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#endif
#else
#include "gentest/attributes.h"
#include "gentest/runner.h"
#if !GENTEST_MULTI_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN
#include "gentest/mock.h"
#endif
#endif

using namespace gentest::asserts;

[[using gentest: test("multi_imported_sibling/legacy_cpp_importer_two_module_mocks")]]
void legacy_cpp_importer_two_module_mocks() {
#if GENTEST_MULTI_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN
    gentest::skip(GENTEST_MULTI_IMPORTED_SIBLING_LEGACY_IMPORT_BROKEN_REASON);
#else
    gentest::mock<multi_imported_sibling::alpha::Service> alpha_service;
    gentest::mock<multi_imported_sibling::beta::Service> beta_service;

    gentest::expect(alpha_service, &multi_imported_sibling::alpha::Service::compute_alpha).times(1).with(8).returns(21);
    gentest::expect(beta_service, &multi_imported_sibling::beta::Service::compute_beta).times(1).with(5).returns(13);

#if !defined(GENTEST_CODEGEN)
    multi_imported_sibling::alpha::Service &alpha_base = alpha_service;
    multi_imported_sibling::beta::Service &beta_base = beta_service;
    EXPECT_EQ(alpha_base.compute_alpha(8), 21);
    EXPECT_EQ(beta_base.compute_beta(5), 13);
#endif
#endif
}
