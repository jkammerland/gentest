# Story: runtime `skip()` + `xfail()` (expected failure)

## Goal

Allow tests to dynamically skip or declare expected failures from inside the test body, with correct semantics, clear console output, and correct report mappings.

## Motivation / user impact

- Many environments require runtime feature detection (kernel features, permissions, hardware, flaky platform behavior) that cannot be expressed statically via attributes.
- Expected-failure (xfail) is essential for tracking known bugs without making CI red, while still detecting XPASS regressions.

## Scope (must-do)

### Public API

Add to `include/gentest/runner.h`:

- `[[noreturn]] void skip(std::string_view reason = {}, const std::source_location& = std::source_location::current());`
- `void skip_if(bool condition, std::string_view reason = {}, const std::source_location& = std::source_location::current());`
- `void xfail(std::string_view reason = {}, const std::source_location& = std::source_location::current());`
- `void xfail_if(bool condition, std::string_view reason = {}, const std::source_location& = std::source_location::current());`
- Optional: `XFailScope` RAII helper if needed to avoid “sticky” xfail state.

### Semantics

- `skip()`:
  - Immediately aborts the current test and marks it **skipped** with an optional reason.
  - If failures were already recorded in the test, do **not** downgrade to skipped; remain failed (optionally append a note).
  - If called with no active context, follow existing “no active test context” fatal policy.

- `xfail()`:
  - Marks the test as expected-to-fail.
  - Outcome mapping:
    - Failures recorded and/or exception thrown ⇒ **XFAIL** (does not fail process).
    - No failures and no exception ⇒ **XPASS** (counts as failure).
  - Precedence: skip wins over xfail; static attribute skip wins over everything (test never starts).

### Output / reporting

- Console:
  - Print `[ XFAIL ]` for expected failures and `[ XPASS ]` for unexpected passes.
- JUnit:
  - Map XFAIL to `<skipped message="xfail: <reason>">`.
  - Map XPASS to `<failure>` (message `xpass` / includes reason).
- Allure (if enabled):
  - Map XFAIL as skipped with a label/property carrying xfail reason (or encode into skip reason).
  - Map XPASS as failed.

### Test coverage

- Add a small dedicated suite (recommended: `tests/outcomes/`) with cases:
  - `runtime_skip_simple`
  - `runtime_skip_if`
  - `xfail_expect_fail`
  - `xfail_throw`
  - `xfail_xpass`
  - `skip_overrides_xfail`
  - `skip_after_failure_is_fail`
- Extend `cmake/CheckTestCounts.cmake` + `cmake/GentestTests.cmake` to optionally validate `XFAIL`/`XPASS` counts without breaking existing tests.
- Add at least one JUnit check that validates XFAIL/XPASS mapping.

## Acceptance criteria

- `ctest --preset=debug-system --output-on-failure` passes.
- Runtime skip and xfail behaviors match the semantics above.
- New tests validate both console outcomes and exit-code behavior for XFAIL/XPASS.

## Notes / references

- This is based on the proposed design in `results/05_tests_and_feature_gaps.md:1`.

