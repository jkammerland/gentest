// Intentionally invalid gentest attributes for lint-only negative test

[[using gentest: test("smoke/invalid"), unknown]]
void will_fail() {}

