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
test from the installed consumer path, but the failure turned out to be another
regression-harness mismatch, not a product bug:

- the script still assumed the generated `main.cpp` wrapper would always be
  `gentest_codegen/tu_0000_main.gentest.h`
- the installed consumer now builds additional generated TUs first, so the
  `main.cpp` wrapper is not guaranteed to stay `tu_0000`
- the Windows launcher dereference logic was still narrower than the reopened
  contract check wanted

So the closure condition for this story is: keep proving explicit
`--scan-deps-mode=<mode>` propagation through the generated build graph without
depending on one specific TU ordinal or one fragile Windows launcher shape.

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

Validation on `2026-04-14` showed the reopened failure was still a
build-graph-inspection mismatch, not a dropped scan-deps flag:

- local Linux:
  `ctest --preset=debug-system -R '^gentest_codegen_public_module_imports$' --output-on-failure`
  -> passed after updating the regression to:
  - match any generated per-TU header under `gentest_codegen/tu_<n>_*.gentest.h`
  - keep dereferencing Windows launcher wrappers before checking the underlying
    `gentest_codegen` command text
- native Windows deep path:
  `ctest --preset=debug-system --output-on-failure -R '^gentest_codegen_public_module_imports$'`
  from `C:\Users\ai-dev1.DESKTOP-NMRV6E3\repos\gentest-story028-wip`
  -> passed with the same harness update

The current evidence says the installed public-module consumer path still
preserves explicit `--scan-deps-mode=<mode>` propagation, and story `031` can
close as a regression-check alignment fix rather than a product-layer change.
