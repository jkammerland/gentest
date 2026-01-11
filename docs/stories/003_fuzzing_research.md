# Story: fuzzing support (research: FuzzTest / Centipede)

## Goal

Decide how `gentest` should support fuzzing, with a strong preference for first-class integration with Google
FuzzTest (Centipede engine), leveraging `gentest_codegen` to reduce boilerplate.

## Motivation / user impact

- Pure random fuzzing can miss corner cases; feedback-guided engines (coverage, cmp tracing/value profiling) are the
  baseline expectation.
- Users want fuzzing to be:
  - **easy to author** (minimal macros / wiring),
  - **easy to build/run** (CMake-friendly, good defaults),
  - **easy to reproduce** (stable artifacts: corpus, repro args),
  - **optional** (does not force heavyweight deps in “unit test only” builds).

## Scope (must-do)

### Research questions

1. **FuzzTest / Centipede fundamentals**
   - What is the canonical “fuzz target” API surface in FuzzTest (e.g., `FUZZ_TEST`, domains, corpus, repro flags)?
   - Confirm Centipede is the default engine in fuzzing mode and understand the supported CLI surface for
     long-running fuzzing vs short “unit” executions.

2. **Integration shape with `gentest`**
   - Option A (preferred): generate FuzzTest registration from `[[using gentest: fuzz(...)]]` declarations via codegen,
     producing a dedicated fuzzing executable (or one per suite/target).
   - Option B: embed/bridge FuzzTest into the `gentest` runner (single binary; `--fuzz=...`), if feasible without
     pulling in gtest or if FuzzTest offers a non-gtest runner.
   - Identify the minimal integration that avoids “double frameworks” complexity for users.

3. **Codegen opportunity**
   - Determine what we can infer from the C++ signature to auto-generate reasonable domains:
     - `void f(std::span<const std::uint8_t>)` (bytes) vs typed parameters (`int`, `std::string`, structs).
   - Decide how users override defaults:
     - attribute-level domain expressions, dictionaries, max-size/timeout budgets, corpus dir.

4. **Build system story**
   - CMake:
     - How to make FuzzTest optional via `find_package(fuzztest CONFIG)` (or similar) without forcing network downloads.
     - Confirm required flags for fuzzing mode (FuzzTest CMake quickstart mentions `-DFUZZTEST_FUZZING_MODE=on` and
       `fuzztest_setup_fuzzing_flags(<target>)`).
   - Cross-compiling:
     - Identify whether fuzzing is supported/meaningful in cross builds; document host-only limitation when needed.

5. **If we build our own engine**
   - Identify what a “gentest-native” engine would need to be competitive:
     - coverage collection, corpus management, minimization, dictionaries, comparison tracing/value profiling,
       crash/timeout triage, sanitizer integration, multi-process execution.
   - Estimate effort and long-term maintenance vs integrating FuzzTest/Centipede.

### Deliverables

- A short design note (can live at the end of this story) answering:
  - recommended direction (A/B/custom),
  - proposed public authoring API (attributes + expected function signatures),
  - proposed CMake API surface,
  - test/CI strategy (fast smoke in CI vs long-running fuzz jobs).

## Out of scope (nice-to-have)

- Implementing the fuzzing feature (tracked in follow-up stories).
- Perfect 1:1 mapping of all FuzzTest features on day one (advanced domain composition, custom mutators, etc.).
- Long-running fuzzing infrastructure/CI (cluster runs, corpus centralization).

## Acceptance criteria

- This story document is updated with:
  - a clear recommendation (integrate FuzzTest/Centipede vs native engine),
  - a concrete proposed developer experience (example annotations + how to run),
  - a prioritized set of follow-up implementation stories.

## Notes / references

- Google FuzzTest repository: https://github.com/google/fuzztest
- FuzzTest CMake quickstart (mentions `FUZZTEST_FUZZING_MODE` + `fuzztest_setup_fuzzing_flags()`): https://github.com/google/fuzztest/blob/main/doc/quickstart-cmake.md

## Recommendation (initial)

Prefer **first-class integration with FuzzTest (Centipede)**, using `gentest_codegen` to generate the registration and
minimize user boilerplate. Treat a gentest-native engine as a long-term option only after we’ve validated the authoring
and build/run UX with a mature engine.

### Follow-up stories

1. **Fuzz attribute + discovery + validation**: define `[[using gentest: fuzz(\"...\")]]` and validate supported signatures.
2. **FuzzTest codegen**: emit a `*_fuzz_impl.cpp` that registers fuzz targets (defaults inferred from signature).
3. **CMake integration**: `gentest_add_fuzztest_target(...)` (or similar) that builds a fuzzing executable when FuzzTest is available.
4. **CI smoke**: run fuzz targets in “unit mode” for a bounded number of iterations; leave long-running fuzzing to a dedicated job.

