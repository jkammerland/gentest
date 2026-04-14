# Story: Move Source Transformation Logic Out Of GentestCodegen.cmake

## Goal

Reduce `cmake/GentestCodegen.cmake` to build orchestration and contract wiring,
not source parsing or transformation logic.

This is a build-boundary cleanup story: CMake should decide what to run and
where outputs live, while `gentest_codegen` or a narrower helper layer should
own parsing and transformation behavior.

## Problem

`GentestCodegen.cmake` currently implements logic that behaves like a parser and
source transformer:

- recursive include rewriting
- explicit-mock staging
- mini preprocessor-condition evaluation
- source-shape decisions that are difficult to test and reason about in CMake

That has real costs:

- platform portability becomes a build-language problem
- behavior is split across tool code and CMake logic
- contract tests have to validate parser-like behavior through CMake scripts
- refactors are high-risk because the logic is string-heavy and hard to isolate

## User stories

As a maintainer, I want `GentestCodegen.cmake` to orchestrate codegen rather
than implement parser-like behavior, so build portability bugs are easier to
fix and test.

As a contributor, I want source analysis and transformation rules to live in
one tool-side implementation, so feature work does not require editing complex
CMake string logic.

As a reviewer, I want buildsystem changes to be about dependency wiring and
output ownership, not hidden language processing.

## Scope

In scope:

- `cmake/GentestCodegen.cmake` logic that inspects or rewrites source content
- tool responsibilities that should move into `gentest_codegen`
- narrowing support for hard-to-maintain transformation cases if moving them is
  not justified

This story owns the discovery/inspection/transformation boundary only.

Out of scope:

- removing CMake support
- deleting existing codegen modes without a separate deprecation story
- unrelated runtime cleanup
- render-text deduplication across manifest and TU output shells, which belongs
  to story `024`
- logical generated filename-family decisions, which belong to story `029`
- mock-specific model normalization that does not require a CMake boundary
  change, which belongs to story `030`

## Design direction

Push as much language-aware logic as possible into normal compiled code.

Preferred boundary:

- CMake:
  - select tool/backend
  - pass sources, flags, and output directories
  - register generated outputs and dependencies

- Tool/helper code:
  - inspect source content
  - evaluate transformation rules
  - decide rewritten/generated include and mock surfaces

If some current CMake feature is too expensive to preserve cleanly, prefer an
honest contract reduction over continuing to expand parser-like CMake code.

This story should not be used as a grab-bag for render-text cleanup or filename
reshaping. It is specifically about who owns source-aware transformation logic.

This story owns whether generated and explicit-mock surfaces are discovered,
staged, and wired at all. Story `029` owns the logical basename/stem naming of
those surfaces once they exist.

## Rollout

1. Inventory `GentestCodegen.cmake` behavior by category:
   - pure orchestration
   - path/output bookkeeping
   - source inspection
   - source transformation
2. Migrate one logic bucket at a time into `gentest_codegen` or a narrow helper.
3. Keep contract tests around each migrated behavior until the CMake logic is
   deleted, including:
   - `gentest_codegen_warns_unknown_preprocessor_condition`
   - `gentest_codegen_active_elif_unknown_warn`
   - `gentest_codegen_supported_preprocessor_condition_no_warning`
   - `gentest_codegen_dead_elif_unknown_no_warning`
   - `gentest_codegen_compile_command_condition_warnings`
   - `gentest_codegen_compile_command_macro_scan`
   - `gentest_codegen_resource_dir_from_compdb_compiler`
   - `gentest_codegen_incremental_dependencies`
   - `gentest_module_name_literal_false_match`
   - `gentest_codegen_response_file_expansion`
   - `gentest_codegen_synthetic_compdb_fallback`
   - `gentest_codegen_public_module_imports`
   - `gentest_codegen_external_include_module_resolution`
   - `gentest_explicit_mock_target_surface`
   - `gentest_explicit_mock_target_validation`
   - `gentest_explicit_mock_target_staging`
   - `gentest_explicit_mock_target_install_export`
   - `gentest_codegen_mock_symlink_include`
   - `gentest_codegen_mock_cross_root_include_windows`
4. Document any intentionally reduced support surface instead of silently
   changing behavior.

## Acceptance criteria

- `GentestCodegen.cmake` no longer contains recursive include-rewrite or
  mini-preprocessor-evaluation logic for the migrated behaviors; those
  semantics live in compiled code or are intentionally removed.
- Build-language logic is primarily orchestration, dependency, and path wiring.
- `gentest_codegen_warns_unknown_preprocessor_condition`,
  `gentest_codegen_active_elif_unknown_warn`,
  `gentest_codegen_supported_preprocessor_condition_no_warning`,
  `gentest_codegen_dead_elif_unknown_no_warning`,
  `gentest_codegen_compile_command_condition_warnings`,
  `gentest_codegen_compile_command_macro_scan`,
  `gentest_codegen_resource_dir_from_compdb_compiler`,
  `gentest_codegen_incremental_dependencies`,
  `gentest_module_name_literal_false_match`,
  `gentest_codegen_response_file_expansion`,
  `gentest_codegen_synthetic_compdb_fallback`,
  `gentest_codegen_public_module_imports`,
  `gentest_codegen_external_include_module_resolution`,
  `gentest_explicit_mock_target_surface`,
  `gentest_explicit_mock_target_validation`,
  `gentest_explicit_mock_target_staging`,
  `gentest_explicit_mock_target_install_export`,
  `gentest_codegen_mock_symlink_include`, and
  `gentest_codegen_mock_cross_root_include_windows`
  still pass or are intentionally updated to reflect a documented contract
  reduction.
- Each migrated behavior has one obvious owner: either the buildsystem or the
  tool, not both.

## Latest validation

The main migration slice landed in `1db3854b`, but the refreshed full native
Windows inventory from `2026-04-14` reopened two source-inspection/helper flows
that still belong to this story:

- `gentest_codegen_incremental_dependencies`:
  nested configure-time inspection could not resolve a runnable
  inspect-capable backend after the fixture only had a build-tree
  `gentest_codegen` target
- `gentest_module_name_literal_false_match`:
  the native source-inspection helper bootstrap tried to configure with
  `Unix Makefiles` and no runnable make program/compiler during the fallback
  helper build

That means this story is not closed at the refreshed Windows evidence level
until both nested helper/backend paths are green again.
