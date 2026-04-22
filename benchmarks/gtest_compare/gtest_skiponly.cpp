#include <gtest/gtest.h>

namespace skiponly {

TEST(SkipOnly, Alpha) { GTEST_SKIP() << "not ready"; }

TEST(SkipOnly, Beta) { GTEST_SKIP() << "flaky"; }

} // namespace skiponly
