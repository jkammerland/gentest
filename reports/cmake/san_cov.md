# Sanitizer and Coverage Verification (Impacted CMake Regression Script Targets)

Impacted script targets:

- `regression_outcome_consistency_matrix`
- `gentest_outcome_matrix_stale_junit_guard`
- `gentest_windows_debug_death_skip_gate`
- `gentest_package_workflow_preset`

## Sanitizers (`alusan`)

Commands:

```bash
cmake --preset=alusan
cmake --build --preset=alusan --target gentest_unit_tests gentest_outcomes_tests gentest_regression_shared_fixture_setup_skip gentest_regression_member_shared_fixture_setup_skip_bench_jitter
ctest --preset=alusan --output-on-failure -R '^(regression_outcome_consistency_matrix|gentest_outcome_matrix_stale_junit_guard|gentest_windows_debug_death_skip_gate|gentest_package_workflow_preset)$'
```

Result:

```text
100% tests passed, 0 tests failed out of 4
```

Matched tests:

- `regression_outcome_consistency_matrix` (Passed)
- `gentest_outcome_matrix_stale_junit_guard` (Passed)
- `gentest_windows_debug_death_skip_gate` (Passed)
- `gentest_package_workflow_preset` (Passed)

## Coverage (`coverage`)

Commands:

```bash
cmake --preset=coverage
cmake --build --preset=coverage --target gentest_unit_tests gentest_outcomes_tests gentest_regression_shared_fixture_setup_skip gentest_regression_member_shared_fixture_setup_skip_bench_jitter
ctest --preset=coverage --output-on-failure -R '^(regression_outcome_consistency_matrix|gentest_outcome_matrix_stale_junit_guard|gentest_windows_debug_death_skip_gate|gentest_package_workflow_preset)$'
```

Result:

```text
100% tests passed, 0 tests failed out of 4
```

Matched tests:

- `regression_outcome_consistency_matrix` (Passed)
- `gentest_outcome_matrix_stale_junit_guard` (Passed)
- `gentest_windows_debug_death_skip_gate` (Passed)
- `gentest_package_workflow_preset` (Passed)
