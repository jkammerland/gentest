# Story: Preserve Explicit Scan-Deps Mode In Installed Public-Module Consumers

## Goal

Make installed and in-tree public-module consumer builds preserve the caller's
explicit `GENTEST_CODEGEN_SCAN_DEPS_MODE` choice all the way into the generated
build-time `gentest_codegen` command on every supported platform, including
native Windows.

## Problem

Focused native Windows validation on `2026-04-13` showed
`gentest_codegen_public_module_imports` could fail from a deep checkout after
the path-budget fixes are applied, but that focused slice was later repaired by
teaching the regression to inspect the generated Windows launcher script when
Ninja hid the actual `gentest_codegen` command there.

The refreshed full native Windows inventory from `2026-04-14` reopened the same
test from the installed consumer path, but with a different observable symptom:
the regression could not find the expected
`gentest_codegen/tu_0000_main.gentest.h` custom command in the generated
consumer `build.ninja`.

So the story is reopened as a public-module contract-check failure, not yet as
a confirmed dropped-flag product bug.

## User stories

As a package consumer, I want `GENTEST_CODEGEN_SCAN_DEPS_MODE` to reliably
change the generated build-time `gentest_codegen` command, so `OFF`, `AUTO`,
and `ON` each mean the same thing in installed and in-tree flows.

As a Windows maintainer, I want public-module import tests to fail only for real
module-discovery bugs, not because the build graph silently dropped an explicit
mode override.

As a reviewer, I want the scan-deps-mode contract to be observable in generated
build files and protected by regression tests on Linux and Windows.

## Scope

In scope:

- `cmake/GentestCodegen.cmake` build-time command assembly
- installed-package consumer flows under `tests/consumer`
- `gentest_codegen_public_module_imports` and related contract checks
- Windows-specific command-line or generator behavior if that is where the mode
  is being lost

Out of scope:

- generic Windows path-depth policy, which belongs to story `028`
- explicit mock output naming, which belongs to story `029`
- source-inspection backend ownership, unless it directly blocks mode
  propagation

## Design direction

Treat scan-deps mode as a first-class part of the codegen command contract.

Preferred approach:

1. trace where `GENTEST_CODEGEN_SCAN_DEPS_MODE` is resolved for installed and
   in-tree consumers
2. ensure command assembly always emits `--scan-deps-mode=<mode>` when the
   caller selected one explicitly
3. verify that Windows Ninja generation preserves the flag in the concrete
   build graph
4. keep `AUTO`/`ON`/`OFF` behavior aligned between direct CLI tests and
   build-time generated commands

## Rollout

1. reproduce `gentest_codegen_public_module_imports` locally and on native
   Windows.
2. inspect the generated consumer `build.ninja` and any response files to see
   whether the command is being launched indirectly, written under a renamed
   output, or actually missing the explicit mode contract.
3. fix the responsible layer:
   - launcher/build-graph inspection if the test is stale
   - output-contract wiring if the expected command shape moved
   - command assembly or cache propagation only if the mode is really dropped
4. rerun the existing public-module import regression on Linux and Windows.
5. add or tighten assertions so the generated build graph keeps exposing the
   explicit mode contract.

## Acceptance criteria

- `gentest_codegen_public_module_imports` passes on native Windows and the
  existing Linux lane.
- The generated build-time `gentest_codegen` command includes
  `--scan-deps-mode=<mode>` whenever `GENTEST_CODEGEN_SCAN_DEPS_MODE` is set
  explicitly by the consumer configure step.
- The regression still verifies `OFF`, `AUTO`, and `ON` semantics instead of
  merely checking that a build succeeds.

## Latest validation

Focused validation on `2026-04-13` showed the earlier Windows failure was a
launcher-inspection mismatch, not a dropped codegen flag:

- local Linux:
  `ctest --preset=debug-system -R gentest_codegen_public_module_imports --output-on-failure`
  -> passed
- native Windows deep path:
  `ctest --test-dir build/debug-system --output-on-failure -R '^gentest_codegen_public_module_imports$'`
  -> passed after teaching the regression to inspect the generated `.bat`
  launcher when Ninja hides the actual `gentest_codegen` command there

The refreshed full native Windows matrix on `2026-04-14` reopened
`gentest_codegen_public_module_imports` with a different check failure:

- the generated installed-consumer `build.ninja` no longer matched the current
  regression's expectation for a custom command producing
  `gentest_codegen/tu_0000_main.gentest.h`
- this evidence is enough to reopen the story, but not enough to conclude yet
  whether the fault is stale test inspection, output-contract drift, or a real
  lost `--scan-deps-mode=<mode>` propagation bug
