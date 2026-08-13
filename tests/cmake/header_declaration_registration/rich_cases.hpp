#pragma once

#include <gentest/async.h>
#include <gentest/runner.h>
#include <type_traits>

namespace header_declaration_registration {

[[using gentest: test("header_declaration/rich/parameter"), parameters(value, 1, 2)]]
void rich_parameter(int value);

[[using gentest: test("header_declaration/rich/rows"), parameters_pack((left, right), (1, 2), (3, 4))]]
void rich_rows(int left, int right);

[[using gentest: test("header_declaration/rich/range"), range(value, 1, 1, 3)]]
void rich_range(int value);

template <typename T>
[[using gentest: test("header_declaration/rich/template"), template(T, int, long)]]
inline void rich_template() {
    gentest::expect(std::is_integral_v<T>);
}

[[using gentest: test("header_declaration/rich/async"), req("REQ-107"), owner("codegen")]]
gentest::async_test<void> rich_async();

[[using gentest: bench("header_declaration/rich/bench"), items_per_call(2)]]
void rich_bench();

[[using gentest: jitter("header_declaration/rich/jitter")]]
void rich_jitter();

[[using gentest: test("header_declaration/rich/death"), death]]
void rich_death();

} // namespace header_declaration_registration
