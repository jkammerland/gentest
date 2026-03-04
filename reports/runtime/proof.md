# Runtime/Reporting Proof Report

Date: 2026-03-04
Branch: `fix/runtime-reporting-hardening`

## 1) JUnit miss of real failures from fallback path (`src/runner_test_executor.cpp` + `src/runner_reporting.cpp`)
- Reproducible: **yes**
- Red command:
  - `ctest --preset=debug-system --output-on-failure -R "regression_runtime_reporting_"`
- Red snippet:
  - `Expected substring not found in file: 'failures="1"'`
  - `Content: <testsuite ... failures="0" ... name="regressions/runtime_reporting/fallback_assertion_failure" ...>`

## 2) GitHub annotation escaping gaps for file/title values (Windows-style path + punctuation)
- Reproducible: **yes**
- Red command:
  - `ctest --preset=debug-system --output-on-failure -R "regression_runtime_reporting_"`
- Red snippet:
  - Expected encoded annotation not found.
  - Actual output before fix:
    - `::error file=C:/repo,win/src/runtime_reporting_case.cpp,line=77,title=regressions/runtime_reporting/gha,title:punct::xpass: runtime-reporting-annotation`

## 3) JUnit CDATA invalidity when message contains `]]>`
- Reproducible: **yes**
- Red command:
  - `ctest --preset=debug-system --output-on-failure -R "regression_runtime_reporting_"`
- Red snippet:
  - `Found unsplit CDATA terminator in JUnit output`
  - `<failure><![CDATA[... runtime-reporting-cdata-token ]]> marker]]></failure>`

## 4) JUnit write/open failure handling visibility
- Reproducible: **yes**
- Red command:
  - `ctest --preset=debug-system --output-on-failure -R "regression_runtime_reporting_"`
- Red snippet:
  - `Expected exit code 1, got 0.`
  - Output before fix:
    - `[ PASS ] regressions/runtime_reporting/pass_for_junit_io_visibility (0 ms)`
    - `Summary: passed 1/1; failed 0; skipped 0; xfail 0; xpass 0.`

## 5) Stale manual `Case.line` literals in measured regression files
- Reproducible: **yes**
- Red command:
  - `ctest --preset=debug-system --output-on-failure -R "regression_runtime_reporting_"`
- Red snippet:
  - `Found hard-coded numeric Case.line initializers in:`
  - `tests/regressions/bench_assert_propagation.cpp`
  - `tests/regressions/measured_local_fixture_call_teardown_dualfail.cpp`
  - `tests/regressions/measured_local_fixture_partial_setup_teardown.cpp`
  - `tests/regressions/measured_local_fixture_setup_skip_teardown_fail.cpp`
  - `tests/regressions/measured_local_fixture_setup_skip_teardown_skip.cpp`
  - `tests/regressions/measured_local_fixture_setup_throw_teardown_armed.cpp`

## 6) Optional low-risk: non-atomic pointer/count snapshot in `src/runner_impl.cpp`
- Reproducible: **no (no deterministic reproducer found)**
- Validation note:
  - Existing regression coverage did not provide a deterministic failing case for this path.
  - Current registration model is static-startup oriented; concurrent `register_cases` during `run_all_tests` was not reproduced in this task.
  - No change made for #6 in this pass.
