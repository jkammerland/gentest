#include "gentest/attributes.h"
#include "gentest/runner.h"

namespace outer_a::inner {
struct [[using gentest: fixture(suite)]] SharedFx {};
} // namespace outer_a::inner

namespace outer_b::inner {
[[using gentest: test("fixture_errors/namespace_scope"), fixtures(SharedFx)]]
void bad(outer_a::inner::SharedFx&) {}
} // namespace outer_b::inner
