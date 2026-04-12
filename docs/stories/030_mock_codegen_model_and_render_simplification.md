# Story: Simplify Mock Codegen Model, Discovery, And Render Helpers

## Goal

Reduce duplicated mock-specific codegen logic by normalizing parameter metadata,
qualifier handling, and dispatch emission around a smaller internal model.

This is a mock-pipeline maintainability story. It does not change the manifest
versus TU-wrapper contract and it does not change filename ownership.

## Problem

The current mock codegen path has several small duplication clusters:

- `MockParamInfo` carries redundant boolean state for reference-passing style
- constructor and method discovery rebuild the same parameter-extraction logic
- qualifier normalization is repeated at render time
- pointer-signature formatting duplicates qualifier logic
- dispatch emission is repeated across declaration and definition rendering

Those patterns are individually small, but together they make the mock codegen
path harder to review and easier to let drift.

## User stories

As a maintainer, I want mock parameter and qualifier state normalized once, so
mock-generation fixes do not require parallel edits across discovery and render
helpers.

As a contributor, I want constructor and method mock generation to share the
same parameter and dispatch helpers, so new mock features do not get
implemented twice.

As a reviewer, I want mock render changes to be expressed through a smaller
internal model, so behavior changes are visible instead of buried in repeated
string assembly code.

## Scope

In scope:

- `tools/src/model.hpp`
- `tools/src/mock_discovery.cpp`
- `tools/src/render_mocks.cpp`
- mock-specific internal data shapes and render helpers

Out of scope:

- manifest-vs-TU shared registration/body assembly, which belongs to story
  `024`
- source-transformation ownership between CMake and the tool, which belongs to
  story `025`
- logical generated filename contracts, which belong to story `029`

## Design direction

Normalize once, render many.

Preferred outcomes:

1. one parameter/reference-kind representation instead of overlapping booleans
2. one shared parameter-extraction helper used by constructor and method
   discovery
3. one qualifier-normalization path rather than repeated string cleanup at
   multiple render call sites
4. one shared dispatch-emission helper for declaration and definition output

## Rollout

1. Replace redundant mock parameter state with one normalized representation.
2. Extract shared parameter discovery helpers before changing render code.
3. Consolidate qualifier and pointer-signature formatting around one path.
4. Extract shared dispatch emission for mock methods.
5. Re-run mock-sensitive checks after each step, including:
   - `gentest_core_discovery`
   - `gentest_core_discovery_utils`
   - `gentest_core_render`
   - `gentest_codegen_mock_template_template_pack_direct_expect_runs`
   - `gentest_codegen_mock_unnamed_template_template_builds`
   - `gentest_codegen_mock_defaulted_template_ctor_macro_builds`
   - `gentest_codegen_mock_template_template_ctor_traits_builds`
   - `gentest_explicit_mock_target_surface`
   - `gentest_explicit_mock_target_validation`
   - `gentest_explicit_mock_target_staging`
   - `gentest_explicit_mock_target_install_export`
   - `gentest_codegen_mock_symlink_include`
   - `gentest_module_mock_long_domain_outputs`

## Acceptance criteria

- Mock parameter passing style is represented without overlapping booleans for
  lvalue/rvalue/forwarding semantics.
- Constructor and method mock discovery share one parameter-extraction path.
- Mock qualifier normalization and dispatch emission no longer live in parallel
  duplicated helper blocks.
- `gentest_core_discovery`, `gentest_core_discovery_utils`,
  `gentest_core_render`,
  `gentest_codegen_mock_template_template_pack_direct_expect_runs`,
  `gentest_codegen_mock_unnamed_template_template_builds`,
  `gentest_codegen_mock_defaulted_template_ctor_macro_builds`,
  `gentest_codegen_mock_template_template_ctor_traits_builds`,
  `gentest_explicit_mock_target_surface`,
  `gentest_explicit_mock_target_validation`,
  `gentest_explicit_mock_target_staging`,
  `gentest_explicit_mock_target_install_export`,
  `gentest_codegen_mock_symlink_include`, and
  `gentest_module_mock_long_domain_outputs`
  still pass.
