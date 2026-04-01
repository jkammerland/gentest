# Story: Harden local CI optimization correctness

## Problem

The local CI optimization slice in
[`020_local_ci_runtime_optimization.md`](020_local_ci_runtime_optimization.md)
proved a large wall-time reduction, but the current reuse key and proof
coverage are not yet strong enough to merge safely.

Two classes of risk remain:

- stale producer reuse can hide real regressions when important producer inputs
  change without changing the current semantic key
- the new proof test only exercises one reuse slice, so other producer-key
  variants can regress while the package matrix still reports green

## User stories

As a maintainer, I want producer reuse to invalidate on the important producer
inputs that actually affect the installed package, so package consumer tests do
not silently exercise stale artifacts.

As a reviewer, I want proof tests for each meaningful producer-key class, so
the local CI optimization remains auditable and does not reduce effective
coverage.

As a contributor, I want the default local parallel preset to bound nested
parallelism deterministically, so local test runs stay fast without becoming
machine- or environment-dependent.

## Confirmed gaps

### 1. Toolchain and compiler identity are path-based only

`CheckPackageConsumer.cmake` currently keys reuse on toolchain/compiler paths
and selected cache arguments, but not on toolchain file contents or compiler
identity at those paths.

That allows stale reuse when:

- a toolchain file is edited in place
- a compiler at the same path is replaced or upgraded

### 2. Native-codegen package legs omit codegen sources from the producer surface

The current producer source surface includes:

- `include/gentest/**`
- `src/**`
- relevant `cmake/Gentest*.cmake`

It does not include `tools/**`, even though `_native_codegen` package legs build
and validate an installed `gentest_codegen`.

That can mask regressions in the installed code generator.

### 3. Reuse proof coverage is too narrow

The current proof test only exercises:

- `PACKAGE_TEST_USE_MODULES=OFF`
- `PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE=ON`
- `CONSUMER_LINK_MODE=main_only`

It does not prove reuse correctness for:

- native-codegen producer keys
- module-enabled producer keys
- multi-config exact-config producer keys where applicable

### 4. Nested parallel caps are advisory, not authoritative

The `debug-system*` presets now set `GENTEST_HELPER_BUILD_PARALLEL_LEVEL=1`,
but helper scripts only inject `CMAKE_BUILD_PARALLEL_LEVEL` and
`CTEST_PARALLEL_LEVEL` when those variables are otherwise unset.

That means ambient shell environment can still override the intended bound and
reintroduce oversubscription.

## Scope

This story is a correctness-hardening follow-on for story 020. It does not aim
to find every theoretically possible external input to a nested CMake build.

It does aim to eliminate the concrete stale-reuse cases already identified in
review and to prove the important producer-key variants directly.

## Acceptance criteria

- Producer reuse invalidates when the toolchain file contents change in place.
- Producer reuse invalidates when native-codegen producer inputs change under
  `tools/**`.
- Package proof coverage includes at least:
  - injected-codegen, non-module producer reuse
  - native-codegen, non-module producer reuse
  - injected-codegen, module-enabled producer reuse
- If a multi-config exact-config producer key is supported on the active
  platform, reuse is either proven directly or explicitly excluded with a
  documented reason.
- Nested helper builds and helper-driven nested `ctest` runs obey the intended
  bounded parallel level regardless of ambient shell variables.
- Story 020 is updated so its status/measured-result section does not overclaim
  correctness beyond what the proof tests establish.

## Outcome

This hardening work is now implemented for the concrete risks identified during
review.

Delivered changes:

- producer reuse invalidates on in-place toolchain file edits
- producer reuse invalidates on in-place compiler binary replacement at the
  same path
- native-codegen producer keys include `tools/**`
- proof coverage now directly exercises:
  - injected-codegen, non-module reuse
  - native-codegen, non-module reuse
  - injected-codegen, module-enabled reuse
  - injected-codegen, non-module multi-config exact-config reuse
- helper wrappers force the bounded nested build/test parallel level even when
  the ambient environment exports larger values

Validated locally with:

- targeted proof checks for helper parallel override, producer invalidation,
  and all reuse-key variants
- `ctest --preset=debug-system --output-on-failure -L package`

That leaves story 021 complete for the known stale-reuse and proof-surface gaps
it was created to close.

## Non-goals

- Perfect invalidation for every possible environment-dependent toolchain
  behavior.
- Reworking the entire package helper matrix into a different execution model.
- General CI runtime optimization outside the package/helper path touched by
  story 020.

## Implementation summary

1. Extend the producer semantic key in `CheckPackageConsumer.cmake` with:
   - toolchain file content hashes when present
   - stronger compiler fingerprints, including in-place binary replacement
   - `tools/**` for native-codegen producer keys
2. Expand proof coverage with dedicated helper tests for the missing producer
   key classes.
3. Make helper parallel caps authoritative in the shared helper wrappers.
4. Update story 020 so its status language matches the proof surface now in
   place.
