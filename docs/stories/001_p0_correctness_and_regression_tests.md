# Story: P0 correctness fixes + regression tests

## Goal

Eliminate the highest-risk correctness issues (data races/UB and known logic bugs) and add regression tests so they cannot silently return.

## Motivation / user impact

- Prevent undefined behavior in common runtime operations (logging, CLI parsing).
- Fix an observable math bug (`Approx::rel()` with negative values).
- Reduce “foot-guns” in mocking APIs that can cause dangling references or crashes.

## Scope (must-do)

1. **Runner logging thread-safety**
   - Fix the unsynchronized `dump_logs_on_failure` toggle.
   - Harden `clear_logs()` against `event_lines` / `event_kinds` size mismatches.

2. **Approximate equality correctness**
   - Fix `gentest::approx::Approx::rel()` so relative matching behaves correctly for negative values (scale must be non-negative).
   - Add a regression test that fails on the current bug.

3. **CLI parsing correctness**
   - Fix `parse_szt_c()` overflow/UB hazard on 32-bit `size_t` (no shifting past width; correct overflow detection).

4. **Mocking “foot-guns” (UB/crash hazards)**
   - Make reference-returning `.returns(...)` safe against temporaries (prefer an explicit `returns_ref(...)`/`ReturnRef` style API, or enforce lvalue-only).
   - Make string matchers (`StrContains`, `StartsWith`, `EndsWith`) safe for null `const char*` inputs (no `std::string_view(nullptr)`).

## Out of scope (nice-to-have)

- Redesigning the runner public header to hide `detail::TestContextInfo` (tracked separately).
- Bench/jitter mode completeness and reporting polish (P1/P2).

## Acceptance criteria

- `cmake --preset=debug-system && cmake --build --preset=debug-system && ctest --preset=debug-system --output-on-failure` passes.
- New regression tests exist for:
  - `Approx::rel()` negative-value behavior.
  - Mock reference return safety (compile-time and/or runtime behavior).
  - Null `const char*` handling in string matchers.
- No behavioral regressions in existing suites (counts/tests remain valid).

## Notes / references

- See `results/01_public_api_and_runtime.md:1` and `results/06_bug_risks.md:1` for the initial findings and file locations.

