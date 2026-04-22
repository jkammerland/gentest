# Story: Unify Manifest And TU-Wrapper Codegen Emission Pipelines

## Goal

Collapse the duplicated tool-side emission logic for manifest mode and TU-wrapper
mode into one shared render core, so new codegen behavior is implemented once
and reused across both output styles.

This is the larger follow-up to the narrower generated-output naming cleanup in
`021_runtime_context_and_codegen_dedup.md`.

## Problem

`tools/src/emit.cpp` still contains parallel emission paths that rebuild much of
the same rendering work:

- wrapper assembly
- trait-array assembly
- case-entry emission
- placeholder replacement and text stitching

That duplication creates drift risk:

- one mode can receive fixes while the other lags behind
- review cost is high because similar output logic is spread across multiple
  blocks
- output-sensitive bugs are harder to reason about because there is no single
  render core

## User stories

As a maintainer, I want manifest mode and TU-wrapper mode to share one render
pipeline, so changes to generated code shape happen in one place.

As a contributor, I want emission code to separate shared content generation
from output-shell selection, so new features do not require copy-pasting the
same logic into both modes.

As a reviewer, I want output drift risk between the two codegen modes to be
structurally reduced rather than managed by discipline alone.

## Scope

In scope:

- tool-side emission in `tools/src/emit.cpp` and related render helpers
- shared wrapper and registration content generation for manifest and TU modes
- output-equivalence checks where the same logical content should match across
  modes

This story owns shared generated text and fragment assembly only.

Out of scope:

- deleting manifest mode
- changing the supported output contracts without explicit tests
- unrelated CMake orchestration cleanup
- output-path naming and logical generated filename families, which belong to
  stories `021` and `029`
- source discovery or transformation boundary changes, which belong to story
  `025`
- mock-specific model/discovery/render cleanup that does not depend on
  manifest-vs-TU shell unification, which belongs to story `030`

## Design direction

Use one internal representation for generated content and keep mode-specific
differences at the output-shell boundary.

This story should answer one question only: how does one shared set of already
discovered data turn into manifest-mode versus TU-mode generated text?

Preferred shape:

1. collect and normalize the shared generated fragments once
2. render shared registration/body fragments once
3. let manifest mode and TU-wrapper mode choose only:
   - which shells/files receive those fragments
   - which include/import wrappers differ
   - which mode-specific prologue or epilogue is required

Avoid replacing two duplicated pipelines with a third helper layer that still
reconstructs the same strings twice under different names.

## Rollout

1. Inventory the duplicated fragment assembly sites in `emit.cpp`.
2. Group them by truly shared content versus mode-specific shell behavior.
3. Extract shared rendering helpers or a shared intermediate representation.
4. Diff generated outputs before and after the refactor for representative
   suites in both manifest and TU modes, including:
   - `gentest_codegen_emit_namespaced_attrs_mixed_std_first`
   - `gentest_codegen_emit_namespaced_attrs_mixed_scoped_first`
   - `gentest_codegen_emit_axis_generators`
   - `gentest_codegen_emit_parameters_pack`
   - `gentest_codegen_emit_fixture_resolution`
   - `gentest_codegen_emit_template_fixture_resolution`
   - `gentest_codegen_emit_template_parser_edges`
   - `gentest_codegen_emit_type_quoting`
   - `gentest_codegen_tu_depfile_aggregation`
   - `gentest_codegen_manifest_header_shared_fixture_visibility`
   - `gentest_tu_register_symbol_collision`
   - `gentest_tu_wrapper_source_props`
5. Keep output-sensitive tests around the same fixtures while the pipeline is
   being collapsed.

## Acceptance criteria

- Shared registration/body fragment assembly for manifest and TU-wrapper modes
  is implemented through one shared render helper or intermediate-representation
  path rather than duplicated mode-specific blocks.
- Manifest mode and TU-wrapper mode differ mainly in file-shell placement,
  include/import wrappers, and other truly mode-specific shells.
- `gentest_core_render`, `gentest_core_discovery`, `gentest_codegen_check_valid`,
  `gentest_codegen_emit_namespaced_attrs_mixed_std_first`,
  `gentest_codegen_emit_namespaced_attrs_mixed_scoped_first`,
  `gentest_codegen_emit_axis_generators`,
  `gentest_codegen_emit_parameters_pack`,
  `gentest_codegen_emit_fixture_resolution`,
  `gentest_codegen_emit_template_fixture_resolution`,
  `gentest_codegen_emit_template_parser_edges`,
  `gentest_codegen_emit_type_quoting`,
  `gentest_codegen_tu_depfile_aggregation`,
  `gentest_codegen_manifest_header_shared_fixture_visibility`, and
  `gentest_tu_register_symbol_collision`, and
  `gentest_tu_wrapper_source_props`
  remain green.
- New changes to generated output shape can be implemented without editing two
  parallel pipelines.

## Latest validation

Closure audit on `2026-04-14` found the current branch already satisfies the
story's unification question:

- audit result:
  `results/story024_audit_r1.md`
  -> shared registration/body fragment assembly is centralized through
  `render_registration_core()` / `apply_registration_core()` in `emit.cpp`
  plus the shared render helpers in `render.cpp`; remaining manifest-vs-TU
  differences are shell/orchestration level

Fresh acceptance validation from a clean `debug-system` worktree build also
stayed green:

- `ctest --preset=debug-system --output-on-failure -R '^(gentest_core_render|gentest_core_discovery|gentest_codegen_check_valid|gentest_codegen_emit_namespaced_attrs_mixed_std_first|gentest_codegen_emit_namespaced_attrs_mixed_scoped_first|gentest_codegen_emit_axis_generators|gentest_codegen_emit_parameters_pack|gentest_codegen_emit_fixture_resolution|gentest_codegen_emit_template_fixture_resolution|gentest_codegen_emit_template_parser_edges|gentest_codegen_emit_type_quoting|gentest_codegen_tu_depfile_aggregation|gentest_codegen_manifest_header_shared_fixture_visibility|gentest_tu_register_symbol_collision|gentest_tu_wrapper_source_props)$'`
  -> `15/15` passed

Story `024` can close on the current branch state without another
implementation slice.
