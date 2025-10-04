// Intentionally invalid gentest attributes for lint-only negative test

[[using gentest: test("smoke/invalid"), unknown]]
void will_fail() {}

// 1) Unknown template parameter name in attribute
template <typename X>
[[using gentest: test("smoke/invalid/template-unknown-name"), template(T, int)]]
void invalid_template_name() {}

// 2) Missing attribute for one parameter (should error)
template <typename T, int N>
[[using gentest: test("smoke/invalid/template-missing-attr"), template(T, int)]]
void invalid_missing_attr() {}

// 3) More than two parameters; missing one attribute
template <typename A, typename B, typename C>
[[using gentest: test("smoke/invalid/triad-missing"), template(A, int), template(B, float)]]
void triad_missing() {}

// 4) Interleaved order; missing one attribute
template <typename A, int N, typename B>
[[using gentest: test("smoke/invalid/interleaved-missing"), template(A, int), template(NTTP: N, 1)]]
void interleaved_missing() {}

// 5) Extra unknown parameter alongside correct ones
template <typename T>
[[using gentest: test("smoke/invalid/extra-unknown"), template(T, int), template(U, float)]]
void extra_unknown() {}

// 6) Duplicate template attribute for same parameter
template <typename T>
[[using gentest: test("smoke/invalid/duplicate"), template(T, int), template(T, long)]]
void duplicate_template_attr() {}
