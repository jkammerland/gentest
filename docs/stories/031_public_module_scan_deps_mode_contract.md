# Story: Preserve Explicit Scan-Deps Mode In Installed Public-Module Consumers

## Goal

Make installed and in-tree public-module consumer builds preserve the caller's
explicit `GENTEST_CODEGEN_SCAN_DEPS_MODE` choice all the way into the generated
build-time `gentest_codegen` command on every supported platform, including
native Windows.

## Problem

Focused native Windows validation on `2026-04-13` showed
`gentest_codegen_public_module_imports` still fails from a deep checkout after
the path-budget fixes are applied.

The failure is not path-depth. The generated consumer `build.ninja` omits
`--scan-deps-mode=OFF` even when configure-time cache state sets
`GENTEST_CODEGEN_SCAN_DEPS_MODE=OFF`, while still propagating
`--clang-scan-deps=...`.

That means the installed-package consumer build graph is not honoring the
explicit scan-deps-mode contract consistently.

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
   exactly where the mode is being dropped.
3. fix the command assembly or cache propagation layer.
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
