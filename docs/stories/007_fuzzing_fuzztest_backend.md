# Story: fuzzing backend v1 (FuzzTest / Centipede-friendly)

## Goal

Implement a `gentest` fuzzing backend using Google FuzzTest (and optionally Centipede when available),
without leaking FuzzTest symbols into `gentest` public headers.

## Motivation / user impact

- Provides a mature, coverage-guided fuzzing engine and corpus management.
- Lets users author fuzz targets in `gentest` style (attributes + codegen), without hand-writing macros/boilerplate.

## Scope (must-do)

### Codegen output (FuzzTest backend)

- Emit a generated TU that registers fuzz targets using FuzzTest’s **macro-free** API:
  - `fuzztest::RegisterFuzzTest(fuzztest::GetRegistration(...))`
  - Avoid `FUZZ_TEST(...)` macro token-pasting limitations (suite/test token constraints).
- Default domain behavior:
  - typed signatures: rely on FuzzTest default `Arbitrary<T>()` unless overridden by attributes.
  - bytes signatures: provide a wrapper that maps to a FuzzTest-friendly container type (e.g. `std::vector<std::uint8_t>`)
    then calls the user function with `std::span`.

### Build system (CMake)

- Add a CMake helper to build a fuzzing executable only when enabled, e.g.:
  - `gentest_attach_fuzztest_codegen(TARGET <exe> OUTPUT <gen.cpp> SOURCES <...>)`
  - or `gentest_add_fuzztest_target(...)` that creates a dedicated `*_fuzz` executable.
- The helper must:
  - use `find_package(fuzztest CONFIG QUIET)` (no network/downloads by default),
  - link to `fuzztest::fuzztest` and `fuzztest::fuzztest_gtest_main` (or the minimal required targets),
  - keep fuzzing flags target-scoped (do not mutate global `CMAKE_CXX_FLAGS`).

### Runtime UX

- Document recommended commands for:
  - listing fuzz tests (`--list_fuzz_tests`),
  - running a specific fuzz test (`--fuzz=Suite.Test`),
  - time-bounded fuzzing for CI (`--fuzz_for=... --time_budget_type=...`),
  - corpus database control (`--corpus_database=...`).

### Tests

- Add generator tests that validate:
  - fuzz targets are registered with stable names (not token-pasted macro names),
  - unsupported signatures fail fast with clear diagnostics.
- Add an opt-in integration test that builds a fuzz executable when FuzzTest is available (gated behind a CMake option).

## Out of scope (nice-to-have)

- Building Centipede as a separate tool in CMake (unless required for the chosen backend mode).
- Distributed fuzzing orchestration.
- Advanced domain composition and seed providers beyond basic attribute wiring.

## Acceptance criteria

- With FuzzTest installed/available:
  - A fuzz target authored with `[[using gentest: fuzz("...")]]` builds into a runnable fuzzing executable.
  - Running `--list_fuzz_tests` shows the generated fuzz tests.
- Without FuzzTest available:
  - default `gentest` build/test workflows remain unchanged.
- `include/gentest/` contains no FuzzTest references.

## Notes / references

- FuzzTest/Centipede long read: `docs/fuzzing/fuzztest_centipede_long_read.md`
- Upstream CMake quickstart: https://github.com/google/fuzztest/blob/main/doc/quickstart-cmake.md

