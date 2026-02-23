# Story: runtime regression hardening + CMake check script updates

## Goal

Capture the recent runtime correctness fixes and the regression harness updates so behavior and test intent stay explicit in docs.

## Behavior updates

1. Bench/jitter call-phase failures now fail measured cases
   - Assertions and recorded expectation failures in benchmark/jitter call loops are surfaced as call failures.
   - The measured case exits non-zero instead of silently continuing.

2. Shared fixture lookup is non-reentrant during setup
   - While a shared fixture is still setting up, lookup returns `nullptr` with
     `fixture initialization in progress`.
   - This prevents exposing partially initialized shared fixture instances.

3. Per-TU header collision checks are case-insensitive
   - Codegen fails fast if two inputs map to the same generated header name
     ignoring case.

## Regression tests and checks

- `regression_bench_assert_failure_propagates`
  - Verifies benchmark assertion failures are propagated.
- `regression_shared_fixture_reentry_no_timeout`
  - Verifies reentrant shared fixture lookup is blocked during setup and does not hang.
  - Uses `cmake/CheckNoTimeout.cmake` with `EXPECT_RC=0`.
- `gentest_tu_header_case_collision`
  - Verifies case-insensitive TU output header collision detection.
  - Uses `cmake/CheckTuHeaderCaseCollision.cmake`.

## CMake helper script notes

- `CheckNoTimeout.cmake`
  - Required: `PROG`
  - Optional: `ARGS`, `TIMEOUT_SEC`, `EXPECT_RC`
- `CheckTuHeaderCaseCollision.cmake`
  - Required: `PROG`, `BUILD_ROOT`
  - Optional: `TARGET_ARG`, `EXPECT_SUBSTRING`
