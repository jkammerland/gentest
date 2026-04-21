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
- the remaining story-tracked cleanups are `015`, `032`, and the cross-cutting
  cleanup campaign `037`

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

1. Story `032`: split the generated-code/devkit boundary from the installed
   runtime-detail compatibility layer.
   - Keep `gentest/runner.h` and `import gentest;` unchanged.
   - Reduce generated-code and runtime-internal support dependence on broad
     installed detail headers.
   - Decide and implement the end state for the `gentest/fixture.h` /
     `gentest/registry.h` compatibility shims.
   - Preserve package/module downstream coverage on every slice.

2. Story `015`: finish non-CMake parity for explicit mocks and supported
   module paths.
   - Keep Meson textual-only and explicitly fail-fast for modules.
   - Polish installed Xmake helper and source-package Bazel helper surfaces.
   - Preserve the explicit two-step mock/codegen model.

3. Story `037`: codegen contract cleanup campaign (parallel track).
   - Runs alongside `032` / `015` and picks up after each gating story closes.
   - Wave 4 (independent, cheap): keep `DEPRECATIONS.md` current, keep warning
     coverage for `NO_INCLUDE_SOURCES` and `EXPECT_SUBSTRING`, guard the install
     tree against legacy `share/cmake/gentest/scan_inspector/`, and remove
     `EXPECT_SUBSTRING` after the documented deprecation target. Does not wait
     on `033` or `015`.
   - Wave 1 (unblocked by `033`): delete configure-time source inspector probe and
     related extraction helpers from CMake while preserving textual wrapper
     semantics.
   - Wave 2 (gated on `015`): rewrite `xmake/gentest.lua` and
     `build_defs/gentest.bzl` as thin manifest consumers; delete
     `xmake/templates/*.in` and `meson/*.in`.
   - Wave 3 (this branch is the `2.0.0` removal branch): hard-remove legacy
     `OUTPUT=...` manifest mode, `NO_INCLUDE_SOURCES`, CLI
     `--no-include-sources`, and `EXPECT_SUBSTRING`.

## Closure criteria

- `032`: generated-code support depends on a smaller explicit devkit contract
  rather than the broad installed fixture/registry runtime-detail layer.
- `032`: runtime-internal generated-code support includes are migrated to the
  same smaller devkit contract.
- `032`: `gentest/fixture.h` and `gentest/registry.h` are either reduced
  further or left only as explicitly unstable compatibility shims with a
  smaller payload than today.
- `032`: `gentest_public_module_surface`,
  `gentest_public_module_detail_hidden`,
  `gentest_runtime_shared_context_exports`, and
  `gentest_package_consumer_legacy_detail_contract` remain green.
- `032`: `gentest_install_only_codegen_default`,
  `gentest_package_consumer_workdir_isolation`, and
  `gentest_package_consumer_executable_path` remain green.
- `032`: generated-code package and non-CMake downstream lanes remain green.
- `033`: closed; follow-up CMake deletion work is now part of `037` wave 1.
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

Take `037` wave 1 next now that `033` closed, then continue with `032` and
`015`. Story `037` wave 2 follows `015`; wave 3 is handled on this branch as
the `2.0.0` removal branch.

That ordering follows the current evidence:

- the refreshed `022` inventory surfaced the native Windows blockers, and the
  reopened `025`, `028`, and `031` slices are now re-closed by validated
  implementation work
- `023` and `026` are now closed at their current scope
- `033` is closed and provides the internal module boundaries for `037`
- `032` is a future devkit/generated-code boundary cleanup with a different
  risk profile than the already-closed public-surface and helper-simplification
  stories
