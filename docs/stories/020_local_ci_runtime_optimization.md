# Story: Reduce local CI wall time without losing coverage

## Problem

The full local `debug-system` pass currently has strong coverage, but the wall
time is dominated by repeated nested configure/build/install helper tests rather
than by the main handwritten test executables.

A recent full local run completed with:

- `355/355` passing, `0` failing
- total real time: `675.77s`
- largest label buckets:
  - `modules`: `393.66 sec*proc`
  - `cmake`: `315.99 sec*proc`
  - `package`: `255.18 sec*proc`
  - `codegen`: `169.78 sec*proc`

The slowest single legs were mostly package-consumer and module/codegen helper
tests that rebuild tiny producer/consumer sandboxes from scratch.

## User stories

As a maintainer, I want the full local CI pass to finish faster without
dropping validation surface, so performance improvements do not come at the
cost of confidence.

As a contributor, I want repeated local runs to reuse deterministic intermediate
artifacts where the semantic inputs are unchanged, so iteration time improves
after the first clean pass.

As a reviewer, I want runtime optimizations to keep the existing behavioral
matrix intact, so faster execution does not hide lost coverage behind broader,
weaker tests.

## Constraints

- Coverage must stay materially the same.
- Producer/consumer contract checks must still validate install/export
  correctness, runtime execution, list output, bench/jitter entry points, and
  module/mock paths where applicable.
- Optimizations must remain deterministic and safe under concurrent CTest
  execution.
- Helper build roots must stay compact enough for Windows path limits.

## Current hotspots

### 1. Package consumer matrix

`CheckPackageConsumer.cmake` already computes a semantic producer root keyed by
package mode, compilers, build type/config, and source-surface hashes.
However, each invocation still reconfigures and rebuilds/reinstalls the producer
before testing the consumer, even when another test with the same producer key
already completed successfully.

This is the clearest first optimization because the script already has:

- a stable producer hash
- a dedicated producer root
- a cross-process lock

So the missing piece is reuse, not identity.

### 2. Serial helper-heavy test execution

The `debug-system` test preset does not currently declare bounded parallel test
execution, so the large helper-test matrix mostly runs one CTest process at a
time.

This is likely the next major wall-time reduction, but it should be introduced
carefully with resource limits or bounded jobs so nested builds do not
oversubscribe the machine.

### 3. Multi-leg module/codegen helpers

Some expensive helpers already batch multiple legs under one producer build
(for example `CheckCodegenPublicModuleImports.cmake`). Others still repeat
nearly identical setup across many CTest cases. Those scripts are good follow-on
targets once the package-consumer reuse path is proven.

## Implemented slice

The first delivered slice is slightly broader than the original phase-1 plan,
but it still preserves the existing validation surface:

- add producer reuse to `CheckPackageConsumer.cmake`
- broaden producer invalidation so reuse keys track the installed package source
  surface and relevant configure environment inputs
- add a hermetic proof test that invokes the same package-consumer leg twice and
  asserts that only the second run reuses the cached producer
- add bounded local CTest parallelism to the `debug-system*` test presets
- serialize the three `xmake` helper tests with a shared resource lock so the
  new preset-level parallelism remains deterministic

This keeps the consumer configure/build/run steps per test while removing
repeated producer work and allowing safe overlap across the helper-heavy matrix.

## Acceptance criteria

- `CheckPackageConsumer.cmake` reuses an existing producer build/install when
  the semantic producer key is unchanged and the cached install is complete.
- Reuse is guarded by a deterministic stamp/check under the existing producer
  lock, so concurrent test execution remains safe.
- A dedicated proof test verifies that the second identical invocation reports
  producer reuse.
- Existing package-consumer tests continue to pass with the optimization.

## Status

The currently implemented slice proves a meaningful local wall-time reduction
and now has direct correctness hardening for the main producer-key variants.

Implemented safeguards:

- reuse only occurs after a successful producer configure/build/install and
  ready-stamp check under the existing producer lock
- the producer semantic key now includes:
  - package mode inputs
  - compiler and config selection
  - generator/toolchain/make-program environment
  - toolchain file content fingerprints
  - compiler identity fingerprints
  - hashes of the installed-package source surface under `include/gentest`,
    `src`, relevant `cmake/Gentest*.cmake` files, and `tools/**` for
    native-codegen producer keys
- proof coverage now includes:
  - injected-codegen, non-module producer reuse
  - native-codegen, non-module producer reuse
  - injected-codegen, module-enabled producer reuse
  - injected-codegen, non-module multi-config exact-config producer reuse
- dry-run invalidation checks prove:
  - toolchain file contents change the producer key
  - native-codegen `tools/**` edits change the producer key
  - replacing a compiler binary in place at the same path changes the producer
    key
- the proof tests use isolated nested build roots, so reruns stay hermetic
- the `debug-system*` presets run with bounded `execution.jobs = 4`
- nested helper builds and helper-driven nested `ctest` runs are forced to the
  bounded `GENTEST_HELPER_BUILD_PARALLEL_LEVEL`
- `xmake` helper tests use a shared `RESOURCE_LOCK` to avoid cross-test
  interference under preset parallelism

## Measured result

Fresh-tree comparison on this machine:

- serial full suite:
  - configure/build tree: `build/ci-serial`
  - command: `ctest --test-dir build/ci-serial --output-on-failure --parallel 1`
  - result: `356/356` passed, `1` skipped
  - total real time: `711.23s`
- parallel full suite with the implemented preset behavior:
  - configure/build tree: `build/ci-parallel`
  - command:
    `GENTEST_HELPER_BUILD_PARALLEL_LEVEL=1 ctest --test-dir build/ci-parallel --output-on-failure --parallel 4`
  - result: `356/356` passed, `1` skipped
  - total real time: `254.56s`

That is a `64.2%` reduction in clean full-suite wall time:

- `(711.23 - 254.56) / 711.23 = 64.2%`

Supported-preset validation on the repo-local build also passed:

- command: `ctest --preset=debug-system --output-on-failure`
- result: `356/356` passed, `1` skipped
- total real time: `231.30s`

## Follow-on work

Remaining candidates after this slice:

1. Batch more package-consumer matrix legs under fewer CTest entries while
   preserving per-leg assertions inside the helper script.
2. Apply the same cached-producer pattern to other producer/consumer helper
   scripts that still rebuild identical installations.
3. Tune the preset job count or resource model per platform if `4` is not the
   best default across the supported local environments.
