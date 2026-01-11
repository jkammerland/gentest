# Story: fuzzing support (FuzzTest / Centipede integration)

## Goal

Add a first, end-to-end fuzzing workflow for `gentest` based on Google FuzzTest (Centipede engine), with an
attribute-driven authoring model and minimal boilerplate.

## Motivation / user impact

- Users should be able to turn a function into a fuzz target as easily as writing a `gentest` case today.
- The integration should feel “native” to `gentest` (clang-based discovery + codegen), while still using a mature fuzzing
  engine for coverage guidance, corpus management, and minimization.

## Scope (must-do)

### Authoring API

- Add a fuzzing attribute form (final naming TBD):
  - `[[using gentest: fuzz("suite/name")]]`
- Supported fuzz target signatures (pick one minimal set for v1):
  - **bytes**: `void f(std::span<const std::uint8_t> data)` (or `(const std::uint8_t*, std::size_t)`), or
  - **typed**: `void f(T1, T2, ...)` with default domains inferred (e.g. `fuzztest::Arbitrary<T>()`).
- Provide a way to override defaults without forcing macros:
  - `domain(param, "<expr>")` or similar attribute payloads (string expressions emitted into generated code).
  - optional: `dict("...")`, `max_len(N)`, `timeout_ms(N)`, `corpus("path")`.

### Codegen

- Discovery:
  - Extend `gentest_codegen` to find fuzz targets (separate from unit tests/benches).
  - Validate signatures and emit clear diagnostics (generator failure is preferred over silently ignoring).
- Emission:
  - Generate a dedicated translation unit for fuzz targets (e.g. `<target>_fuzz_impl.cpp`) that:
    - includes the user’s sources (or a configurable subset),
    - includes FuzzTest headers,
    - registers each fuzz target with `FUZZ_TEST(...)` and appropriate domains.
- Keep generated fuzz artifacts separate from `gentest` unit test impl generation.

### Build integration (CMake)

- Add CMake helper(s) that make FuzzTest optional:
  - Prefer `find_package(fuzztest CONFIG)` when available in the toolchain/prefix.
  - Do not require FetchContent/network for default builds.
- Provide an ergonomic entry point, e.g.:
  - `gentest_add_fuzztest_target(TARGET <name> SOURCES ... OUTPUT ... )`
  - or `gentest_attach_fuzztest_codegen(TARGET <exe> ...)` (mirroring `gentest_attach_codegen`).
- The helper should:
  - wire codegen generation of `<target>_fuzz_impl.cpp`,
  - link to the FuzzTest main/runner target(s),
  - support FuzzTest’s fuzzing mode via user-configurable CMake options.

### Runtime UX

- Document how to:
  - list fuzz targets,
  - run a specific fuzz target,
  - run bounded “unit mode” executions for CI smoke,
  - run long fuzzing sessions (and where the corpus/repro artifacts land).

### Tests

- Add generator unit/negative tests that validate:
  - fuzz signature validation errors are clear,
  - fuzz targets are discovered and emitted.
- Add a build-only smoke test that compiles generated fuzz registration when FuzzTest is available (gated behind an
  option so default CI remains fast and dependency-light).

## Out of scope (nice-to-have)

- A single unified runner that mixes `gentest` tests and FuzzTest fuzzing in the same binary/process.
- Full exposure of every FuzzTest domain feature in v1.
- Distributed fuzzing / shared corpora infra.

## Acceptance criteria

- With FuzzTest available, a user can:
  - annotate fuzz targets,
  - build a fuzzing executable via a `gentest` CMake helper,
  - run a single fuzz target with the engine’s CLI,
  - reproduce crashes using saved artifacts.
- Without FuzzTest available, default `gentest` builds continue to work (fuzzing is opt-in).
- `ctest --preset=debug-system --output-on-failure` passes on default configurations.

## Notes / references

- FuzzTest repository: https://github.com/google/fuzztest
- CMake quickstart: https://github.com/google/fuzztest/blob/main/doc/quickstart-cmake.md

