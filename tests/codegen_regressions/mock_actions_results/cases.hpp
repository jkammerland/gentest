#pragma once

#include "gentest/attributes.h"

namespace fixture {

[[using gentest: test("mock_actions_results/non_default_constructible_result")]]
void non_default_constructible_result();

[[using gentest: test("mock_actions_results/move_only_result")]]
void move_only_result();

[[using gentest: test("mock_actions_results/stateful_action")]]
void stateful_action();

[[using gentest: test("mock_actions_results/out_parameters")]]
void out_parameters();

[[using gentest: test("mock_actions_results/owning_arguments")]]
void owning_arguments();

[[using gentest: test("mock_actions_results/reference_without_action_death"), death]]
void reference_without_action_death();

[[using gentest: test("mock_actions_results/unexpected_reference_call_death"), death]]
void unexpected_reference_call_death();

[[using gentest: test("mock_actions_results/unexpected_value_call_death"), death]]
void unexpected_value_call_death();

} // namespace fixture
