# Runtime Reporting Sanitizer + Coverage Verification

## Scope
- Focused on new/changed runtime-reporting regressions only.
- Built affected targets only (plus transitive deps):
  - `gentest_regression_runtime_reporting`
  - `gentest_regression_bench_assert`
  - `gentest_regression_measured_local_fixture_call_teardown_dualfail`
  - `gentest_regression_measured_local_fixture_partial_setup_teardown`
  - `gentest_regression_measured_local_fixture_setup_skip_teardown_fail`
  - `gentest_regression_measured_local_fixture_setup_skip_teardown_skip`
  - `gentest_regression_measured_local_fixture_setup_throw_teardown_armed`

## Regex Used
```regex
^(regression_runtime_reporting_.*|regression_(bench|jitter)_assert_failure_propagates|regression_(bench|jitter)_std_exception_failure_propagates|regression_(bench|jitter)_fail_failure_propagates|regression_(bench|jitter)_skip_failure_propagates|regression_(bench|jitter)_setup_skip_noninfra_(exit_zero|junit_skip)|regression_(bench|jitter)_local_fixture_(partial_setup_failure_teardown|setup_throw_teardown_armed|call_teardown_dualfail_.*|setup_skip_teardown_fail_.*|setup_skip_teardown_skip_.*))$
```

## Sanitizer (`alusan`)
Commands:
```bash
cmake --preset=alusan
cmake --build --preset=alusan --target gentest_regression_runtime_reporting gentest_regression_bench_assert gentest_regression_measured_local_fixture_call_teardown_dualfail gentest_regression_measured_local_fixture_partial_setup_teardown gentest_regression_measured_local_fixture_setup_skip_teardown_fail gentest_regression_measured_local_fixture_setup_skip_teardown_skip gentest_regression_measured_local_fixture_setup_throw_teardown_armed
ctest --preset=alusan -N -R '<regex-above>'
ctest --preset=alusan -R '<regex-above>'
```

Results:
- Selected tests: `45`
- Passed: `45`
- Failed: `0`
- Summary line: `100% tests passed, 0 tests failed out of 45`

## Coverage (`coverage`)
Commands:
```bash
cmake --preset=coverage
cmake --build --preset=coverage --target gentest_regression_runtime_reporting gentest_regression_bench_assert gentest_regression_measured_local_fixture_call_teardown_dualfail gentest_regression_measured_local_fixture_partial_setup_teardown gentest_regression_measured_local_fixture_setup_skip_teardown_fail gentest_regression_measured_local_fixture_setup_skip_teardown_skip gentest_regression_measured_local_fixture_setup_throw_teardown_armed
ctest --preset=coverage -N -R '<regex-above>'
ctest --preset=coverage -R '<regex-above>'
```

Results:
- Selected tests: `45`
- Passed: `45`
- Failed: `0`
- Summary line: `100% tests passed, 0 tests failed out of 45`

## Feasibility Note
- Full-preset test execution was intentionally not run.
- Request asked for affected-target verification where feasible; this run covers the new/changed runtime-reporting regressions directly while avoiding unrelated preset-wide test time.
