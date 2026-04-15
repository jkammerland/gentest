# Simplification Execution Plan

This plan is the next-step sequence for finishing the remaining simplification
stories on the integration branch `simplify-runtime-codegen-dedup`.

It assumes the current branch state, not raw worktree ancestry:

- active story worktree commits may live on different SHAs
- `git cherry simplify-runtime-codegen-dedup <story-branch>` is the source of
  truth for whether a story slice is already integrated by patch equivalence

## Current branch truth

- `021` through `031` are now treated as done at the current evidence level
- the remaining future cleanup is story `032`

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
## Practical next move

Take story `032` only when the repo is ready for another package/module
contract cleanup pass.

That ordering follows the current evidence:

- the refreshed `022` inventory surfaced the native Windows blockers, and the
  reopened `025`, `028`, and `031` slices are now re-closed by validated
  implementation work
- `023` and `026` are now closed at their current scope
- `032` is a future devkit/generated-code boundary cleanup with a different
  risk profile than the already-closed public-surface and helper-simplification
  stories
