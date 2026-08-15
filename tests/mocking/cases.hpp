#pragma once

#include "gentest/attributes.h"
#include "gentest/runner.h"
#include "helper.hpp" // use mock<T> in non-annotated helper code
#include "public/gentest_textual_suite_mocks.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

using namespace gentest::asserts;
namespace mocking {

static_assert(!std::is_default_constructible_v<gentest::mock<NoDefault>>);
static_assert(std::is_nothrow_constructible_v<gentest::mock<NoDefault>, int>);
static_assert(std::is_constructible_v<gentest::mock<NoDefault>, int, long>);
static_assert(!std::is_nothrow_constructible_v<gentest::mock<NoDefault>, int, long>);
static_assert(std::is_nothrow_constructible_v<gentest::mock<NoDefault>, short, int>);

static_assert(!std::is_default_constructible_v<gentest::mock<NeedsInit>>);
static_assert(std::is_nothrow_constructible_v<gentest::mock<NeedsInit>, int>);
static_assert(std::is_constructible_v<gentest::mock<NeedsInit>, int, long>);
static_assert(!std::is_nothrow_constructible_v<gentest::mock<NeedsInit>, int, long>);
static_assert(std::is_nothrow_constructible_v<gentest::mock<NeedsInit>, short>);
static_assert(std::is_nothrow_constructible_v<gentest::mock<NeedsInit>, short, int>);

static_assert(std::is_nothrow_constructible_v<gentest::mock<TemplateTemplateCtorTarget>, std::array<int, 2>>);

[[using gentest: test("mocking/interface/returns")]]
void interface_returns();

[[using gentest: test("mocking/interface/returns_ref")]]
void interface_returns_ref();

[[using gentest: test("mocking/interface/returns_matches")]]
void interface_returns_matches();

[[using gentest: test("mocking/interface/reset")]]
void interface_reset();

[[using gentest: test("mocking/interface/non_default_ctor")]]
void interface_non_default_ctor();

[[using gentest: test("mocking/concrete/invokes")]]
void concrete_invokes();

[[using gentest: test("mocking/concrete/non_default_ctor")]]
void concrete_non_default_ctor();

[[using gentest: test("mocking/concrete/static_member")]]
void concrete_static_member();

[[using gentest: test("mocking/concrete/invokes_matches")]]
void concrete_invokes_matches();

[[using gentest: test("mocking/concrete/predicate_match")]]
void concrete_predicate_match();

[[using gentest: test("mocking/concrete/template_member_expect_int")]]
void concrete_template_member_expect_int();

[[using gentest: test("mocking/concrete/template_member_signature_collision")]]
void concrete_template_member_signature_collision();

[[using gentest: test("mocking/concrete/template_member_instantiation_split")]]
void concrete_template_member_instantiation_split();

[[using gentest: test("mocking/concrete/direct_expect_signature_collision")]]
void concrete_direct_expect_signature_collision();

[[using gentest: test("mocking/concrete/direct_constant_expect_signature_collision")]]
void concrete_direct_constant_expect_signature_collision();

[[using gentest: test("mocking/template/forwarding_alias")]]
void template_forwarding_alias();

[[using gentest: test("mocking/template/direct_unique_template_member_expect")]]
void direct_unique_template_member_expect();

[[using gentest: test("mocking/template/template_template_member_expect")]]
void template_template_member_expect();

[[using gentest: test("mocking/template/template_template_ctor")]]
void template_template_ctor();

[[using gentest: test("mocking/template/template_template_pack_direct_expect")]]
void template_template_pack_direct_expect();

[[using gentest: test("mocking/crtp/bridge")]]
void crtp_bridge();

[[using gentest: test("mocking/crtp/bridge_matches")]]
void crtp_bridge_matches();

[[using gentest: test("mocking/matchers/eq_any")]]
void matchers_eq_any();

[[using gentest: test("mocking/matchers/in_range")]]
void matchers_in_range();

[[using gentest: test("mocking/matchers/not")]]
void matchers_not();

[[using gentest: test("mocking/matchers/where_call")]]
void matchers_where_call();

[[using gentest: test("mocking/move_only/with_eq")]]
void move_only_with_eq();

[[using gentest: test("mocking/move_only/refwrap_by_value")]]
void move_only_refwrap_by_value();

[[using gentest: test("mocking/matchers/str_contains")]]
void matchers_str_contains();

[[using gentest: test("mocking/matchers/starts_ends")]]
void matchers_starts_ends();

[[using gentest: test("mocking/matchers/cstr_null_safe")]]
void matchers_cstr_null_safe();

[[using gentest: test("mocking/matchers/near")]]
void matchers_near();

[[using gentest: test("mocking/matchers/ge_anyof")]]
void matchers_ge_anyof();

[[using gentest: test("mocking/concurrency/adopted_ordered_dispatch")]]
void concurrency_adopted_ordered_dispatch();

[[using gentest: test("mocking/concurrency/late_mutation_ignored_after_runtime_start")]]
void concurrency_late_mutation_ignored_after_runtime_start();

[[using gentest: test("mocking/nice/unexpected_ok")]]
void nice_unexpected_ok();

} // namespace mocking
