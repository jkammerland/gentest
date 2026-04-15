# Native Windows Inventory For Story 022

## Purpose

This file is the authoritative checked-in inventory artifact for
`022_windows_ci_portability_followups.md`.

Refresh it from a fresh native Windows:

`ctest --preset=debug-system --output-on-failure`

run before creating or updating non-path Windows follow-up stories.

## Inventory

Latest checked native-Windows evidence in this repo is a fresh full
`ctest --preset=debug-system --output-on-failure` run from `2026-04-14`.

Run summary:

- checkout root:
  `C:\Users\ai-dev1.DESKTOP-NMRV6E3\repos\gentest-b1ce47da`
- configure: passed
- build: passed
- ctest: `501` total, `5` failed, `10` skipped, `486` passed
- log root:
  `C:\Users\ai-dev1.DESKTOP-NMRV6E3\repos\gentest-story022-logs-b1ce47da`

Still failing:

- `gentest_codegen_incremental_dependencies`:
  source-inspection backend/helper resolution during nested configure-time
  inspection; `GentestCodegen.cmake` reported
  `gentest source inspection could not resolve a runnable inspect-capable backend`
  after the nested fixture resolved `gentest_codegen` only as a build-tree
  target. -> story `025`
- `gentest_module_name_literal_false_match`:
  source-inspection helper bootstrap on native Windows; the fallback helper
  configure path tried to use `Unix Makefiles` without a runnable make program
  or configured compiler, so `_gentest_build_native_scan_inspector(...)`
  failed before inspection ran. -> story `025`
- `gentest_module_mock_additive_visibility`:
  Windows path-budget / dyndep rename failure in a nested module-mock helper
  build from the deep checkout root; `clang-scan-deps` wrote `.obj.ddi.tmp`
  and `cmake -E rename` failed with `The system cannot find the path specified.`
  -> story `028`
- `gentest_module_header_unit_import_preamble`:
  same Windows path-budget / dyndep rename failure class in another nested
  module/mock helper build from the deep checkout root. -> story `028`
- `gentest_codegen_public_module_imports`:
  installed public-module scan-deps contract check failure; the full-matrix
  Windows run could not find the expected `gentest_codegen/tu_0000_main.gentest.h`
  custom command in the generated consumer `build.ninja`, so the regression
  could not prove the explicit scan-deps-mode contract. -> story `031`

Already green in the refreshed full matrix:

- `gentest_codegen_mock_template_template_pack_direct_expect_runs`
- `gentest_codegen_mock_unnamed_template_template_builds`
- `gentest_codegen_mock_defaulted_template_ctor_macro_builds`
- `gentest_codegen_mock_template_template_ctor_traits_builds`
- `gentest_xmake_xrepo_consumer`
- `gentest_module_mock_long_domain_outputs`

Still green in focused earlier slices and not re-failing in the full matrix:

- `gentest_codegen_public_module_imports` is no longer a path-depth failure;
  the surviving failure is owned by story `031`, not `028`
- the previously fixed deep-path mock fixture set from story `028` remains green
- story `029` output-contract validation still covers the long-domain mock case

## Notes

- This refreshed inventory closes story `022`: every surviving failure now has
  a concrete owner story (`025`, `028`, or `031`).
- Story `028` is reopened by the full matrix even though the earlier focused
  deep-path slice stayed green; the new failures are additional deep-checkout
  module/mock shapes.
- Story `025` is reopened by native Windows helper/backend bootstrap failures in
  configure-time source inspection.
- Story `031` is reopened by the full-matrix installed public-module import
  check failure.
