# Story: Make Windows Path-Depth And Short-Root Behavior Robust

## Goal

Make native Windows helper, module-scan, and downstream-harness builds reliable
from ordinary checkout roots, not only from specially shortened paths such as
`C:\\g`.

This is the dedicated owner for Windows path-depth and short-root behavior. It
complements the broader non-path Windows portability work in
`022_windows_ci_portability_followups.md`.

## Problem

Recent native Windows validation showed a specific failure pattern:

- helper roots can already be hashed compactly in `tests/CMakeLists.txt`
- some helper scripts still append long fixture names to that root
- nested fixture builds then add:
  - `build/`
  - `generated/` and `generated/mocks/`
  - long target names
  - `CMakeFiles/<target>.dir/...`
  - module-scan artifacts such as `.obj.ddi.tmp`

The result is that a deep checkout path can push intermediate Windows build
artifacts past what some tools in the subprocess chain handle reliably. The
failure may surface as:

- `clang-scan-deps` / dyndep write failures
- `cmake -E rename` failures for `.ddi.tmp`
- generic `The system cannot find the path specified` errors even though the
  real cause is path depth

The current `C:\\g` short-path workaround proves the failing slice can succeed
when the same artifacts are shorter.

## User stories

As a maintainer, I want native Windows helper fixtures to pass from normal
workspace paths, so CI and local repros do not depend on special checkout
locations.

As a contributor, I want Windows fixture failures to reflect real product bugs
instead of harness path-depth limits.

As a reviewer, I want path-shortening policy to be centralized and intentional,
so portability fixes are stable rather than ad hoc.

## Scope

In scope:

- helper build-root policy in `tests/CMakeLists.txt`
- fixture helper scripts under `tests/cmake/scripts/`
- nested fixture target/build/output naming that materially affects Windows path
  depth
- public-module and Xmake harness path-shortening where the failing behavior is
  path-depth or dyndep-file related
- Windows-specific build knobs such as `CMAKE_OBJECT_PATH_MAX` where justified

This story owns physical path-budget policy: roots, internal aliases, repeated
directory layers, and object-path limits.

Out of scope:

- unrelated Windows ABI or launcher issues, which belong to story `022`
- macOS SDK/toolchain setup
- changing human-readable `CTest` names unless needed for helper internals
- changing logical generated filename families or tool/CMake filename contracts
  unless coordinated through story `029`

## Design direction

Shorten paths at the structural layers that repeat the most.

Preferred changes:

1. keep the outer helper root hashed and compact
2. stop re-appending long fixture names under that root when an internal short
   alias would do
3. shorten helper-internal build/output directories where the human-readable
   name adds little value
4. apply Windows-specific object-path limits where module/dyndep flows still
   generate long intermediates
5. preserve human-readable top-level test names while allowing shorter internal
   fixture build aliases

This story may shorten internal roots and aliases, but story `029` owns
cross-layer logical filename contracts such as generated basename/stem
definitions.

## Rollout

1. Measure the longest failing helper and module-scan artifact paths.
2. Shorten the helper-script `_work_dir` policy first.
3. Add compact internal aliases for the longest fixture targets if root
   shortening is not enough.
4. Apply `CMAKE_OBJECT_PATH_MAX` or similar guardrails where they reduce known
   Windows-only path blowups.
5. Re-run the previously failing Windows fixture-build checks from both:
   - a normal deep checkout path
   - a short path
6. Re-run the Windows path-sensitive downstream checks that already use short
   roots, including:
   - `gentest_module_mock_additive_visibility`
   - `gentest_module_header_unit_import_preamble`
   - `gentest_codegen_mock_cross_root_include_windows`
   - `gentest_xmake_textual_consumer_registration`
   - `gentest_codegen_public_module_imports`
   - `gentest_module_mock_long_domain_outputs`
   - `gentest_xmake_textual_consumer`
   - `gentest_xmake_module_consumer`
   - `gentest_xmake_xrepo_consumer`

## Acceptance criteria

- `gentest_codegen_mock_template_template_pack_direct_expect_runs`,
  `gentest_codegen_mock_unnamed_template_template_builds`,
  `gentest_codegen_mock_defaulted_template_ctor_macro_builds`, and
  `gentest_codegen_mock_template_template_ctor_traits_builds`
  pass from a normal deep Windows checkout root, not only from `C:\\g`.
- Helper scripts no longer recreate long fixture-name subdirectories beneath an
  already-shortened helper root.
- `gentest_codegen_mock_cross_root_include_windows`,
  `gentest_module_mock_additive_visibility`,
  `gentest_module_header_unit_import_preamble`,
  `gentest_codegen_public_module_imports`,
  `gentest_module_mock_long_domain_outputs`,
  `gentest_xmake_textual_consumer_registration`,
  `gentest_xmake_textual_consumer`, and
  `gentest_xmake_module_consumer`, and
  `gentest_xmake_xrepo_consumer`
  no longer require ad hoc short-root workarounds beyond one shared path policy.
- Helper fixtures and Windows path-sensitive harnesses consume one shared
  short-root/path-shortening policy instead of per-script ad hoc roots.

## Latest validation

Focused native Windows deep-path validation on `2026-04-13` moved the targeted
slice from seven failures down to two remaining non-path contract failures.
After the follow-up story `029` and `031` slices the focused set is fully
green:

- fixed in this story:
  - `gentest_codegen_mock_template_template_pack_direct_expect_runs`
  - `gentest_codegen_mock_unnamed_template_template_builds`
  - `gentest_codegen_mock_defaulted_template_ctor_macro_builds`
  - `gentest_codegen_mock_template_template_ctor_traits_builds`
  - `gentest_xmake_xrepo_consumer`
- fixed later under other stories:
  - `gentest_module_mock_long_domain_outputs` -> story `029`
  - `gentest_codegen_public_module_imports` -> story `031`
- still failing, but owned elsewhere:
  - none in the focused deep-path slice

The refreshed full native Windows matrix from `2026-04-14` reopened additional
deep-checkout path-budget failures that still belonged to this story:

- `gentest_module_mock_additive_visibility`
- `gentest_module_header_unit_import_preamble`

Both failed in nested module/mock helper builds with the same Windows dyndep
rename symptom:

- `clang-scan-deps` wrote `.obj.ddi.tmp`
- `cmake -E rename` failed with `The system cannot find the path specified`

That reopened slice is now fixed and revalidated.

What changed:

- the two helper scripts now use a shared compact work-dir policy instead of
  repeating long fixture names under the hashed helper root
- the nested fixture `CMakeLists.txt` files cap `CMAKE_OBJECT_PATH_MAX` on
  Windows
- the shared module-fixture helper now creates a short Windows source view at
  `${SYSTEMDRIVE}/gsrc/...` for nested `add_subdirectory("${GENTEST_SOURCE_DIR}")`
  builds, so the public named-module source paths no longer expand from the
  deep checkout root during dyndep generation

Revalidation:

- Linux:
  `ctest --preset=debug-system --output-on-failure -R '^(gentest_module_mock_additive_visibility|gentest_module_header_unit_import_preamble)$'`
  passed `2/2`
- Native Windows, normal deep checkout:
  `gentest_module_mock_additive_visibility` passed
  `gentest_module_header_unit_import_preamble` passed

At the refreshed Windows evidence level, this story can be treated as closed
again.
