#include <cstdlib>
#include <gentest/async.h>
#include <gentest/bench_util.h>
#include <gentest/runner.h>
#include <type_traits>

namespace header_declaration_registration {

[[using gentest: test("header_declaration/a")]] void               compat_a() {}
[[using gentest: test("header_declaration/b")]] void               compat_b() {}
[[using gentest: test("header_declaration/context_a")]] void       compat_context_a() {}
[[using gentest: test("header_declaration/context_b")]] void       compat_context_b() {}
[[using gentest: test("header_declaration/explicit_mock")]] void   compat_explicit_mock() {}
[[using gentest: test("header_declaration/fallback")]] void        compat_fallback() {}
[[using gentest: test("header_declaration/fixture_parity")]] void  compat_fixture_parity() {}
[[using gentest: test("header_declaration/inline")]] void          compat_inline() {}
[[using gentest: test("header_declaration/macro_pair_one")]] void  compat_macro_pair_one() {}
[[using gentest: test("header_declaration/macro_pair_two")]] void  compat_macro_pair_two() {}
[[using gentest: test("header_declaration/redeclared")]] void      compat_redeclared() {}
[[using gentest: test("header_declaration/same_basename_a")]] void compat_same_basename_a() {}
[[using gentest: test("header_declaration/same_basename_b")]] void compat_same_basename_b() {}
[[using gentest: test("header_declaration/spaced_path")]] void     compat_spaced_path() {}
[[using gentest: test("macro_case_one")]] void                     compat_macro_case_one() {}
[[using gentest: test("macro_case_two")]] void                     compat_macro_case_two() {}

[[using gentest: test("header_declaration/rich/parameter"), parameters(value, 1, 2)]]
void compat_parameter(int value) {
    gentest::expect(value == 1 || value == 2);
}

[[using gentest: test("header_declaration/rich/rows"), parameters_pack((left, right), (1, 2), (3, 4))]]
void compat_rows(int left, int right) {
    gentest::expect(right == left + 1);
}

[[using gentest: test("header_declaration/rich/range"), range(value, 1, 1, 3)]]
void compat_range(int value) {
    gentest::expect(value >= 1 && value <= 3);
}

template <typename T>
[[using gentest: test("header_declaration/rich/template"), template(T, int, long)]]
void compat_template() {
    gentest::expect(std::is_integral_v<T>);
}

[[using gentest: test("header_declaration/rich/async"), req("REQ-107"), owner("codegen")]]
gentest::async_test<void> compat_async() {
    co_await gentest::async::yield();
    gentest::expect(true);
}

[[using gentest: bench("header_declaration/rich/bench"), items_per_call(2)]]
void compat_bench() {
    gentest::doNotOptimizeAway(42);
}

[[using gentest: jitter("header_declaration/rich/jitter")]]
void compat_jitter() {
    gentest::doNotOptimizeAway(42);
}

[[using gentest: test("header_declaration/rich/death"), death]] void compat_death() { std::abort(); }

} // namespace header_declaration_registration
