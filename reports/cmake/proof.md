# CMake/Test Harness Robustness Proof (branch `fix/cmake-harness-robustness`)

## Scope
Validated three claims:
1. `CheckOutcomeConsistencyMatrix` could false-pass on stale JUnit XML.
2. Windows Debug death-test skip gate used `CMAKE_BUILD_TYPE` and was ineffective for multi-config generators.
3. `package` workflow preset described packaging but had no package step.

## Red Phase Evidence (before fixes)

### 1) Stale JUnit false-pass in `CheckOutcomeConsistencyMatrix`
Method:
- Pre-seeded stale XML files in an `OUT_DIR` for all matrix labels.
- Invoked `cmake/CheckOutcomeConsistencyMatrix.cmake` with a fake runner that prints expected summaries/exit codes but **never writes JUnit**.

Observed output:
```text
proof_dir=/tmp/tmp.hrZhMLISvk
rc=0
stale_junit_red_proof_dir=/tmp/tmp.hrZhMLISvk
matrix_script_rc=0
fail.xml
infra_skip_measured.xml
infra_skip_test.xml
pass.xml
skip.xml
xfail.xml
xpass.xml
```
This is a false-pass: script succeeded (`rc=0`) even though JUnit files were stale and no fresh JUnit was produced.

### 2) Windows Debug death-test skip gate ineffective for multi-config
Method:
- Located gate usage and evaluated legacy condition under multi-config-like variables (`CMAKE_CONFIGURATION_TYPES=Debug;Release`, empty `CMAKE_BUILD_TYPE`).

Observed output:
```text
tests/cmake/Regressions.cmake:839:if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS)
tests/CMakeLists.txt:141:if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS)
multi_config_debug_gate=FALSE
```
The condition is false in multi-config context, so debug-only skip cannot activate as intended.

### 3) `package` workflow described packaging but lacked package step
Method:
- Parsed `CMakePresets.json` workflow preset `name=package`.

Observed output:
```text
package_workflow_found= True
package_workflow_steps= ['configure:release', 'build:release', 'test:release']
has_package_step= False
description= Complete release workflow: configure → build → test → package with CPack
```
Description and steps were inconsistent.

## Fixes Applied

1. `cmake/CheckOutcomeConsistencyMatrix.cmake`
- Added pre-delete of per-case JUnit output before execution:
  - `file(REMOVE "${_junit}")`

2. Windows Debug death skip gates (both locations)
- Updated from configure-time build-type gate to config-aware test property:
  - `if(WIN32 AND GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS)`
  - `set_tests_properties(... PROPERTIES DISABLED "$<CONFIG:Debug>")`
- Files:
  - `tests/CMakeLists.txt`
  - `tests/cmake/Regressions.cmake`

3. Package workflow completeness
- Added `packagePresets.release` in `CMakePresets.json`.
- Added `{ "type": "package", "name": "release" }` to workflow preset `name=package`.

## Regression/Lint Checks Added

1. `cmake/CheckOutcomeConsistencyMatrixStaleJunit.cmake`
- Dynamic regression script that seeds stale XML + fake runner with no JUnit output.
- Expects matrix script to fail with missing-JUnit error.

2. `cmake/CheckWindowsDebugDeathSkipGate.cmake`
- Lint script that fails on legacy `CMAKE_BUILD_TYPE` gate pattern.
- Requires config-aware `$<CONFIG:Debug>` gating in both target files.

3. `cmake/CheckPackageWorkflowPreset.cmake`
- Lint script that requires a `package` step in workflow preset `name=package`.

CTest wiring added in `tests/CMakeLists.txt`:
- `gentest_outcome_matrix_stale_junit_guard`
- `gentest_windows_debug_death_skip_gate`
- `gentest_package_workflow_preset`

## Post-Fix Verification

Executed directly:
```text
CheckOutcomeConsistencyMatrixStaleJunit: PASS
CheckWindowsDebugDeathSkipGate: PASS
CheckPackageWorkflowPreset: PASS
```

Configured and executed the wired CTest entries:
```text
ctest --preset=debug-system -N -R "gentest_(outcome_matrix_stale_junit_guard|windows_debug_death_skip_gate|package_workflow_preset)"
  Test #225: gentest_outcome_matrix_stale_junit_guard
  Test #226: gentest_windows_debug_death_skip_gate
  Test #227: gentest_package_workflow_preset

ctest --preset=debug-system --output-on-failure -R "gentest_(outcome_matrix_stale_junit_guard|windows_debug_death_skip_gate|package_workflow_preset)"
  100% tests passed, 0 tests failed out of 3
```

No commit was created.
