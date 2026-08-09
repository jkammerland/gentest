#pragma once

#include "service.hpp"

#if defined(GENTEST_BAZEL_MOCK_DEP)
#include "mock_dep.hpp"
static_assert(gentest_bazel_mock_dep_value == 31);
#endif

namespace downstream::mocks {

using ServiceMock = gentest::mock<downstream::Service>;

} // namespace downstream::mocks
