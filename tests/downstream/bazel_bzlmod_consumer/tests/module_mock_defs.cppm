module;

#if defined(GENTEST_BAZEL_MOCK_DEP)
#include "mock_dep.hpp"
static_assert(gentest_bazel_mock_dep_value == 31);
#endif

export module downstream.bazel.mock_defs;

export import gentest.mock;
export import downstream.bazel.service;

export namespace downstream::mocks {

using ServiceMock = gentest::mock<downstream::Service>;

} // namespace downstream::mocks
