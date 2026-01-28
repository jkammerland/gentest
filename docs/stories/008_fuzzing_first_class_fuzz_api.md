# Story: first-class fuzz API (gentest + FuzzTest)

## Problem statement

`gentest` expectations currently abort when executed inside fuzz targets without preserving rich failure context
(file/line/message/notes). This makes fuzzing runs hard to interpret and limits the usefulness of corpus repros.

## Key FuzzTest endpoints for codegen (with file references)

- Programmatic registration (macro-free):
  - `fuzztest::GetRegistration(...)` and `fuzztest::GetRegistrationWithFixture<Fixture>(...)`
    - Upstream: `fuzztest/internal/registration.h` (documented in `docs/fuzzing/fuzztest_centipede_long_read.md`).
  - `fuzztest::RegisterFuzzTest(...)`
    - Upstream: `fuzztest/internal/registry.h` (documented in `docs/fuzzing/fuzztest_centipede_long_read.md`).
- Runner initialization:
  - `fuzztest::InitFuzzTest(&argc, &argv)`
    - Upstream: `fuzztest/fuzztest_gtest_main.cc`, `fuzztest/init_fuzztest.cc`.
- Macro fallback (if needed for compatibility):
  - `FUZZ_TEST(Suite, Test)` / `FUZZ_TEST_F(Fixture, Test)`
    - Upstream: `fuzztest/fuzztest_macros.h`, `fuzztest/fuzztest.h`.

## User stories

1) S1 - Programmatic fuzz registration from codegen
   - As a user, I want codegen to register fuzz targets without macros so suite/test names can be arbitrary strings
     and remain stable across tokenization rules.
   - Acceptance criteria:
     - Fuzz codegen emits `RegisterFuzzTest(GetRegistration(...))` (and fixture variant where needed).
     - Generated names preserve the original `suite/name` (no macro token constraints).
     - No public `gentest` headers include `fuzztest/*`.
   - Tests to add:
     - Update the codegen golden/emit checks to assert `RegisterFuzzTest` + `GetRegistration` appear in the
       generated fuzz TU and that `FUZZ_TEST` does not.
     - Add a codegen test that preserves a non-identifier name (e.g. `suite/name with spaces`) and confirms
       the generated registration uses the raw string, not macro-safe sanitization.

2) S2 - First-class gentest assertions inside fuzz targets
   - As a user, I want `gentest` expectations inside fuzz targets to surface rich diagnostics without aborting
     the fuzzing process.
   - Acceptance criteria:
     - A `gentest` expectation failure in a fuzz target is converted into a FuzzTest failure with
       file/line/message context.
     - The fuzz run continues to the next input (no hard abort unless explicitly requested).
     - The failure is reproducible via FuzzTest repro mechanisms (e.g. `--repro` or corpus replay).
   - Tests to add:
     - Unit test for the assertion bridge (simulate a failure and verify captured context string).
     - Fuzz integration smoke test (when FuzzTest is available) that runs a tiny target and validates
       the failure is reported via the FuzzTest adaptor (no process abort).

3) S3 - First-class fuzz runner initialization
   - As a user, I want a consistent entrypoint for fuzz executables so FuzzTest flags and behavior are
     correctly initialized without manual boilerplate.
   - Acceptance criteria:
     - A dedicated `gentest_fuzz_main` (or generated main TU) calls `InitFuzzTest(&argc, &argv)` before running.
     - The fuzz target binary supports `--list_fuzz_tests` and other FuzzTest flags out of the box.
   - Tests to add:
     - Build-only integration test for the generated fuzz main + target linkage.
     - If FuzzTest is available, run the binary with `--list_fuzz_tests` and assert the expected entries appear.

4) S4 - Fixture-aware fuzz registration
   - As a user, I want fuzz targets that use fixtures to register with FuzzTest in a way that respects
     setup/teardown semantics.
   - Acceptance criteria:
     - Codegen emits `GetRegistrationWithFixture<Fixture>(...)` for fixture-backed targets.
     - Fixture lifetime options (free/ephemeral/stateful) map to the intended lifecycle in fuzz runs.
   - Tests to add:
     - Codegen test covering a fixture-backed fuzz target (verifies registration path and wrapper shape).
     - Runtime smoke test (if FuzzTest is available) to confirm setup/teardown ordering.

## Notes

- This story focuses on the fuzz API surface and runtime integration; build wiring for fuzz targets is handled
  in `docs/stories/007_fuzzing_fuzztest_backend.md`.
