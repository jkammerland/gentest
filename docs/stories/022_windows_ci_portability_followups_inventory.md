# Native Windows Inventory For Story 022

## Purpose

This file is the authoritative checked-in inventory artifact for
`022_windows_ci_portability_followups.md`.

Refresh it from a fresh native Windows:

`ctest --preset=debug-system --output-on-failure`

run before creating or updating non-path Windows follow-up stories.

## Inventory

Latest checked native-Windows evidence in this repo is the focused deep-path
slice rerun from `2026-04-13`. This is not yet a refreshed full
`ctest --preset=debug-system` inventory, but it is the current source of truth
for the previously failing deep-path/path-sensitive Windows set.

- `gentest_codegen_mock_template_template_pack_direct_expect_runs`:
  helper/path-depth -> `close` (green after story `028` slice)
- `gentest_codegen_mock_unnamed_template_template_builds`:
  helper/path-depth -> `close` (green after story `028` slice)
- `gentest_codegen_mock_defaulted_template_ctor_macro_builds`:
  helper/path-depth -> `close` (green after story `028` slice)
- `gentest_codegen_mock_template_template_ctor_traits_builds`:
  helper/path-depth -> `close` (green after story `028` slice)
- `gentest_xmake_xrepo_consumer`:
  helper/path-depth / downstream harness path budget -> `close` (green after
  story `028` slice)
- `gentest_codegen_public_module_imports`:
  public-module scan-deps contract -> `031_public_module_scan_deps_mode_contract.md`
- `gentest_module_mock_long_domain_outputs`:
  explicit mock output contract -> `029_codegen_output_naming_contract_unification.md`

## Notes

- Remove already-green tests from the inventory instead of carrying them
  forward as stale backlog.
- If every surviving failure maps cleanly to an existing story, close
  `022_windows_ci_portability_followups.md` once the split is done.
