# Simplification Execution Plan

This plan is the next-step sequence for finishing the remaining simplification
stories on the integration branch `simplify-runtime-codegen-dedup`.

It assumes the current branch state, not raw worktree ancestry:

- active story worktree commits may live on different SHAs
- `git cherry simplify-runtime-codegen-dedup <story-branch>` is the source of
  truth for whether a story slice is already integrated by patch equivalence

## Current branch truth

- `021` through `031` are now treated as done at the current evidence level
- `034` and `035` closed with cleanup residue explicitly deferred to `037`
- story `036` is rejected; declaration-only textual registration is no longer
  part of the implementation plan
- story `033` is done: `GentestCodegen.cmake` is now a thin facade over
  installed focused modules in `cmake/gentest/`
- story `032` is done: generated code now uses explicit `registration_runtime`
  / `generated_runtime` devkit headers instead of broad fixture/registry
  runtime-detail headers
- story `015` is done for the supported non-CMake scope
- the remaining story-tracked cleanup is the cross-cutting cleanup campaign
  `037`

## Working rules

1. Use one dedicated worktree per major implementation story.
2. Keep every slice incremental:
   - implement one narrow step
   - `clang-format -i` touched C/C++ files
   - `cmake --build --preset=debug-system`
   - targeted `ctest` slice
   - `./scripts/check_clang_tidy.sh build/debug-system`
   - batch Codex review before merge
3. Cherry-pick proven slices onto `simplify-runtime-codegen-dedup`.
4. After each cherry-pick, verify patch equivalence with `git cherry` instead
   of relying on branch ancestry.
5. Update `docs/stories/STATUS.md` when a story changes state.

## Priority order

### Remaining implementation

1. Story `037`: codegen contract cleanup campaign.
   - Wave 4 (independent, cheap): keep `DEPRECATIONS.md` current, guard the
     install tree against legacy `share/cmake/gentest/scan_inspector/`, and
     keep the `EXPECT_SUBSTRING` hard-error regression green. The
     `EXPECT_SUBSTRING` alias is removed on this `2.0.0` branch.
   - Wave 1 (unblocked by `033`): done. CMake no longer ships the
     configure-time source inspector or scan macro/include-dir collectors, and
     explicit mock aggregate modules are emitted by `gentest_codegen`.
   - Wave 2 (unblocked by `015`): rewrite `xmake/gentest.lua` and
     `build_defs/gentest.bzl` as thin manifest consumers; rewrite the Meson
     textual helper off its `.in` templates; delete `xmake/templates/*.in` and
     `meson/*.in`.
   - Wave 3 (this branch is the `2.0.0` removal branch): user-facing removals
     are landed for legacy `OUTPUT=...` manifest mode, CLI `--output`, CLI
     `--template`, `NO_INCLUDE_SOURCES`, CLI `--no-include-sources`,
     `GENTEST_NO_INCLUDE_SOURCES`, and top-level `EXPECT_SUBSTRING`.

## Closure criteria

- `032`: closed; generated-code support now depends on explicit
  `registration_runtime` / `generated_runtime` devkit contracts, public
  runner/module surfaces stay narrow, direct installed detail headers have
  package/export smoke coverage, and package/non-CMake downstream lanes were
  revalidated for the boundary split under their existing CTest
  tool-availability policies.
- `033`: closed; follow-up CMake deletion work is now part of `037` wave 1.
- `015`: closed for the supported non-CMake scope; Meson remains textual-only,
  Xmake downstream helper coverage is staged through xrepo, and Bazel
  downstream helper coverage is staged through Bzlmod.
- `037`: `DEPRECATIONS.md` exists at the repo root, covers every deprecated
  feature or scheduled cleanup item with warn-since/removal-target information,
  and is linked from `README.md`, `docs/index.md`, and `STATUS.md`.
- `037`: the LOC-reduction targets per wrapper in story `037` are met or
  beaten, measured across the facade file plus any new split modules so the
  campaign cannot succeed by reshuffling alone.
- `037`: every removal wave lands a CI regression that asserts the deprecated
  feature is gone (hard-error for removed options, missing-file for removed
  install paths, missing-subcommand for removed wrapper calls).
- `037`: the public user-facing contracts (`gentest_attach_codegen`,
  `gentest_add_mocks`, `gentest_discover_tests`) stay stable through the
  campaign except for the deliberately removed legacy options.

## Practical next move

Continue with `037` wave 2. Story `015`, story `032`, wave 1, and the
user-facing `2.0.0` removal slice for waves 3 and 4 are already landed on this
branch.

That ordering follows the current evidence:

- the refreshed `022` inventory surfaced the native Windows blockers, and the
  reopened `025`, `028`, and `031` slices are now re-closed by validated
  implementation work
- `023` and `026` are now closed at their current scope
- `033` is closed and provides the internal module boundaries for `037`
- `032` is closed and no longer blocks the non-CMake parity work
- `015` is closed and no longer blocks the cleanup campaign
