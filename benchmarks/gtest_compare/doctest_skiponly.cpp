#include "doctest_compat.hpp"

namespace doctest_skiponly {

TEST_CASE("skiponly/alpha" * doctest::skip(true)) {}

TEST_CASE("skiponly/beta" * doctest::skip(true)) {}

} // namespace doctest_skiponly
