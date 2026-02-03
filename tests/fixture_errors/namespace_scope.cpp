#include "gentest/attributes.h"
#include "gentest/runner.h"

namespace outer_a::inner {
struct [[using gentest: fixture(suite)]] SharedFx {};
struct [[using gentest: fixture(global)]] GlobalFx {};
} // namespace outer_a::inner

namespace outer_b::inner {
[[using gentest: test("fixture_errors/namespace_scope"), fixtures(SharedFx)]]
void bad(outer_a::inner::SharedFx&) {}

[[using gentest: test("fixture_errors/namespace_scope_global"), fixtures(GlobalFx)]]
void bad_global(outer_a::inner::GlobalFx&) {}
} // namespace outer_b::inner
