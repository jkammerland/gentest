// Intentionally invalid gentest attributes for lint-only negative test

[[using gentest: test("smoke/invalid"), unknown]]
void will_fail() {}

// 1) Unknown template parameter name in attribute
template <typename X>
[[using gentest: test("smoke/invalid/template-unknown-name"), template(T, int)]]
void invalid_template_name();

// 2) Interleaved NTTP then type in declaration order (unsupported ordering)
template <int N, typename T>
[[using gentest: test("smoke/invalid/template-interleaved"), template(T, int), template(NTTP: N, 1)]]
void invalid_interleaved();
