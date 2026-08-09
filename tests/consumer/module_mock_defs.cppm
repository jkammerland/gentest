module;

#if defined(GENTEST_BAZEL_MOCK_DEP)
#include "mock_dep.hpp"
static_assert(gentest_bazel_mock_dep_value == 31);
#endif

export module gentest.consumer_mock_defs;

export import gentest.mock;

export import gentest.consumer_service;

export namespace consumer::mocks {

using ServiceMock = gentest::mock<consumer::Service>;

} // namespace consumer::mocks
