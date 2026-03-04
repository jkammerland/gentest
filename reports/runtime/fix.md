# Runtime/Reporting Fix Report

Date: 2026-03-04
Branch: `fix/runtime-reporting-hardening`
Source proof: `reports/runtime/proof.md`

## Scope
Applied fixes only for issues proven reproducible in the proof report (#1-#5). No change was made for optional item #6 because no deterministic reproducer was proven.

## Implemented Fixes

1. JUnit fallback failure accounting now records the real fallback failure message and attaches it to run/report data, so fallback failures are counted and surfaced in JUnit/annotations.
   - Files: `src/runner_test_executor.cpp`, `src/runner_reporting.cpp`

2. GitHub annotation escaping now uses property-safe escaping for `file`/`title` (including `,` and `:`), while keeping message escaping semantics.
   - File: `src/runner_reporting.cpp`

3. JUnit CDATA output now safely splits embedded `]]>` tokens in failure/system-error payloads.
   - File: `src/runner_reporting.cpp`

4. JUnit open/write failures are now promoted to runner-level infra failures and produce non-zero process exit.
   - Files: `src/runner_reporting.cpp`, `src/runner_reporting.h`, `src/runner_orchestrator.cpp`

5. Measured regression cases replaced stale numeric `.line = <literal>` values with `__LINE__`-derived constants.
   - Files:
     - `tests/regressions/bench_assert_propagation.cpp`
     - `tests/regressions/measured_local_fixture_call_teardown_dualfail.cpp`
     - `tests/regressions/measured_local_fixture_partial_setup_teardown.cpp`
     - `tests/regressions/measured_local_fixture_setup_skip_teardown_fail.cpp`
     - `tests/regressions/measured_local_fixture_setup_skip_teardown_skip.cpp`
     - `tests/regressions/measured_local_fixture_setup_throw_teardown_armed.cpp`

## Regression Coverage Added/Updated

- Added runtime-reporting regression executable + tests for:
  - fallback assertion failure reflected in JUnit failure counts
  - GitHub annotation escaping for punctuation in `file`/`title`
  - CDATA splitting for embedded `]]>`
  - JUnit open failure visibility + non-zero exit
- Added CMake checks:
  - `cmake/CheckRuntimeReportingCdata.cmake`
  - `cmake/CheckNoLiteralCaseLines.cmake`
- Added regression source:
  - `tests/regressions/runtime_reporting_regressions.cpp`
- Wired in:
  - `tests/cmake/Regressions.cmake`

## Focused Commands Run (Passing)

```bash
cmake --build --preset=debug-system --target gentest_regression_runtime_reporting
cmake --build --preset=debug-system --target gentest_regression_bench_assert gentest_regression_measured_local_fixture_partial_setup_teardown gentest_regression_measured_local_fixture_setup_throw_teardown_armed gentest_regression_measured_local_fixture_call_teardown_dualfail gentest_regression_measured_local_fixture_setup_skip_teardown_fail gentest_regression_measured_local_fixture_setup_skip_teardown_skip
ctest --preset=debug-system --output-on-failure -R "regression_(runtime_reporting_|bench_assert_)"
```

Result: all matched focused tests passed (6/6).
