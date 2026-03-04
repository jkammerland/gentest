# CMake Harness Fix Report

## Scope
Implemented and validated fixes only for proven issues:
1. Prevent stale JUnit XML from causing false-pass in outcome-matrix script.
2. Make Windows Debug death-test skip gating multi-config aware.
3. Ensure `package` workflow preset actually runs a package step.

## Fixes
- `cmake/CheckOutcomeConsistencyMatrix.cmake`
  - Remove per-case JUnit file before each run:
    - `file(REMOVE "${_junit}")`
- `tests/CMakeLists.txt`
  - Replaced legacy gate:
    - `WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS`
  - With config-aware behavior:
    - `if(WIN32 AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS)`
    - `DISABLED "$<CONFIG:Debug>"`
- `tests/cmake/Regressions.cmake`
  - Same multi-config-aware Debug gating update as above.
- `CMakePresets.json`
  - Added `packagePresets.release`.
  - Added `{"type":"package","name":"release"}` to workflow preset `package`.

## Added Regression/Lint Checks
- `cmake/CheckOutcomeConsistencyMatrixStaleJunit.cmake`
- `cmake/CheckWindowsDebugDeathSkipGate.cmake`
- `cmake/CheckPackageWorkflowPreset.cmake`
- Wired in `tests/CMakeLists.txt` as:
  - `gentest_outcome_matrix_stale_junit_guard`
  - `gentest_windows_debug_death_skip_gate`
  - `gentest_package_workflow_preset`

## Focused Validation (Passing)
```bash
ctest --preset=debug-system --output-on-failure -R 'gentest_(outcome_matrix_stale_junit_guard|windows_debug_death_skip_gate|package_workflow_preset)'
```

Result:
- `3/3` tests passed.

No commit created in this task.
