// Intentionally invalid gentest attributes for lint-only negative test

#include "smoke/invalid_template_attrs.hpp"

[[using gentest: test("smoke/invalid"), unknown]]
void will_fail() {}
