# Simplification Execution Plan

This plan is the next-step sequence for finishing the remaining simplification
stories on the integration branch `simplify-runtime-codegen-dedup`.

It assumes the current branch state, not raw worktree ancestry:

- active story worktree commits may live on different SHAs
- `git cherry simplify-runtime-codegen-dedup <story-branch>` is the source of
  truth for whether a story slice is already integrated by patch equivalence

## Current branch truth

- `021`, `022`, and `027` are treated as done at the current evidence level
- `023` and `026` still need closure work
- `029` and `030` have more integrated progress than their worktree ancestry
  suggests; their remaining work is now mostly closure audit plus smaller final
  cleanup, not a fresh restart

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

1. Story `023`: reduce the remaining installed runtime/fixture leakage.
   - Refresh and confirm `023_public_api_internal_surface_inventory.md` first.
   - Tackle `registry.h` next, then `fixture.h`.
   - Preserve downstream/package coverage on every slice.
   - Explicitly classify what remains unstable `detail` versus fully private.

2. Story `026`: finish helper-driver consolidation.
   - Collapse thin helper wrappers only after the higher-risk product refactors
     above have settled.
   - Keep inventory expectations derived from one declared source of truth.

## Closure criteria by story

- `023`: `registry.h` / `fixture.h` exposure reduced or explicitly justified in
  the inventory artifact
- `024`: one shared render core for manifest and TU generation
- `025`: native Windows source-inspection helper/backend flows stay green in
  the refreshed failing slice
- `026`: smaller helper-driver surface plus one source of truth for inventory
  expectations
- `028`: full-matrix deep-checkout Windows path-budget failures are back to
  green, not only the earlier focused slice
- `029`: explicit audit shows no remaining duplicated logical filename family
  derivation across tool and CMake
- `030`: explicit audit shows the integrated mock slices satisfy the story
  acceptance criteria, or one final cleanup slice lands and proves it
## Practical next move

Take the highest-value unfinished simplification story from `023` or `026`.

That ordering follows the current evidence:

- the refreshed `022` inventory surfaced the native Windows blockers, and the
  reopened `025`, `028`, and `031` slices are now re-closed by validated
  implementation work
- `023` and `026` are the remaining simplification stories
- `023` stays ahead of `026` because public/runtime surface reduction is the
  higher-risk product boundary change, while `026` is mostly build-helper
  consolidation once the public surface is settled
