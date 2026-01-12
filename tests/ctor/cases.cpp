#include "ctor/fixtures.hpp"

namespace ctor {
 
[[using gentest: test("free_fixtures"), fixtures(FreeFx)]]
void free_uses_throwing_fixture(FreeFx&) {}

} // namespace ctor
