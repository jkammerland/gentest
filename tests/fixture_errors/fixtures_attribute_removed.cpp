#include "gentest/attributes.h"

namespace fixture_errors {

struct LegacyFx {};

[[using gentest: test("fixture_errors/legacy_fixtures_attr"), fixtures(LegacyFx)]]
void legacy_fixtures_attr(LegacyFx&) {}

} // namespace fixture_errors
