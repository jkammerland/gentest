# Story: Unify Tool And CMake Codegen Output Naming Contracts

## Goal

Make generated output naming a single cross-layer contract instead of a
"keep these implementations in sync" convention between tool code and
`GentestCodegen.cmake`.

This is the remaining open follow-up from
`021_runtime_context_and_codegen_dedup.md`.

## Problem

The tool-side module-wrapper naming logic is now centralized, but parallel
naming rules still exist across the codebase:

- tool helpers such as `tools/src/generated_output_paths.hpp`
- mock/output helpers such as `tools/src/mock_output_paths.hpp`
- CMake-side derived naming in `cmake/GentestCodegen.cmake`

That creates two risks:

- CMake can predict or materialize different output names than the tool writes
- Windows/path portability fixes can land in one layer but not the other

## User stories

As a maintainer, I want generated file naming to come from one explicit
contract, so tool and CMake changes cannot silently diverge.

As a contributor, I want output-name changes to be made in one place, so new
generated surfaces do not require parallel edits in CMake and tool code.

As a reviewer, I want naming drift to be prevented structurally rather than by
comments and manual synchronization.

## Scope

In scope:

- module-wrapper output naming
- mock registry/impl and domain-header naming
- CMake/tool coordination for derived generated file paths

This story owns logical generated filename and basename/stem contracts only.

Out of scope:

- the larger manifest-vs-TU render-pipeline unification in story `024`
- parser/source-transformation migration in story `025`
- helper-root, short-path, and object-path-budget policy, which belong to story
  `028`
- mock-model or render-helper cleanup that does not change naming contracts,
  which belongs to story `030`

## Design direction

Pick one authoritative naming contract and make the other layer consume it.

Possible acceptable shapes:

1. the tool owns naming and emits a machine-readable manifest CMake consumes
2. a shared compiled helper or library owns naming and both layers call into it
3. CMake owns only directory wiring while the tool returns concrete filenames

What should not remain is duplicated filename-shaping logic in both places with
comments asking future contributors to keep them synchronized manually.

This story is not the place to solve Windows path depth by itself; that belongs
to story `028` unless the fix requires changing the logical filename contract.

Story `025` owns whether generated or explicit-mock surfaces are staged and
wired. This story owns the logical filename families those surfaces use once
they are part of the build contract.

## Rollout

1. Inventory every generated filename family that is currently derived in both
   tool code and `GentestCodegen.cmake`.
2. Choose one authoritative contract for each family.
3. Remove the duplicate derivation sites.
4. Re-run output-sensitive codegen checks, including:
   - `gentest_codegen_output_collision`
   - `gentest_tu_header_case_collision`
   - `gentest_codegen_manifest_depfile_aggregation`
   - `gentest_codegen_manifest_depfile_escaped_paths`
   - `gentest_explicit_mock_target_surface`
   - `gentest_explicit_mock_target_validation`
   - `gentest_explicit_mock_target_staging`
   - `gentest_explicit_mock_target_install_export`
   - `gentest_module_mock_long_domain_outputs`

## Acceptance criteria

- Module-wrapper and mock-output naming are no longer derived independently in
  both tool code and `GentestCodegen.cmake`.
- `gentest_codegen_output_collision`,
  `gentest_tu_header_case_collision`,
  `gentest_codegen_manifest_depfile_aggregation`, and
  `gentest_codegen_manifest_depfile_escaped_paths`,
  `gentest_explicit_mock_target_surface`,
  `gentest_explicit_mock_target_validation`,
  `gentest_explicit_mock_target_staging`,
  `gentest_explicit_mock_target_install_export`, and
  `gentest_module_mock_long_domain_outputs`
  still pass.
- Future output-name changes can be made without editing parallel naming logic
  in both CMake and the tool.

## Latest validation

Focused validation on `2026-04-13` confirmed the Windows-specific
`gentest_module_mock_long_domain_outputs` failure was a launcher-inspection
contract mismatch, not a missing output:

- local Linux: `ctest --preset=debug-system -R gentest_module_mock_long_domain_outputs --output-on-failure` -> passed
- native Windows deep path:
  `ctest --test-dir build/debug-system --output-on-failure -R '^gentest_module_mock_long_domain_outputs$'`
  -> passed after teaching the regression to inspect the generated `.bat`
  launcher when Ninja hides the actual `gentest_codegen` flags behind it
