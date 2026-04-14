# Simplification Execution Plan

This plan is the next-step sequence for finishing the remaining simplification
stories on the integration branch `simplify-runtime-codegen-dedup`.

It assumes the current branch state, not raw worktree ancestry:

- active story worktree commits may live on different SHAs
- `git cherry simplify-runtime-codegen-dedup <story-branch>` is the source of
  truth for whether a story slice is already integrated by patch equivalence

## Current branch truth

- `021`, `022`, and `027` are treated as done at the current evidence level
- `023`, `024`, `026`, `029`, and `030` still need closure work
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

### Phase 1: Closure audits after the Windows blockers

1. Story `029`: perform the final logical filename-family audit.
   - Inventory every remaining generated filename family across
     `GentestCodegen.cmake` and tool code.
   - Decide whether the story can close on current integrated state or whether
     one final implementation slice is still needed.
   - Do not declare closure on audit text alone; closure still requires the
     story's output-sensitive validation slice to stay green.

2. Story `030`: perform the final acceptance-criteria audit.
   - Compare the already integrated mock slices against the story checklist:
     normalized parameter state, shared discovery, qualifier normalization,
     shared dispatch emission.
   - Decide whether the story can close on current integrated state or whether
     one final implementation slice is still needed.
   - Do not declare closure on audit text alone; closure still requires the
     story's mock-sensitive validation slice to stay green.

### Phase 2: Larger remaining implementation after the audits

3. Story `024`: complete manifest-vs-TU emission unification.
   - Finish consolidating the remaining shared fragment assembly in `emit.cpp`.
   - Keep mode differences at the outer shell boundary only.
   - This should happen after the `029` and `030` closure audits so output and
     mock-shape contracts are stable first.

4. Story `023`: reduce the remaining installed runtime/fixture leakage.
   - Refresh and confirm `023_public_api_internal_surface_inventory.md` first.
   - Tackle `registry.h` next, then `fixture.h`.
   - Preserve downstream/package coverage on every slice.
   - Explicitly classify what remains unstable `detail` versus fully private.

5. Story `026`: finish helper-driver consolidation.
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

Take the highest-value unfinished simplification story from `029`, `030`,
`023`, `024`, or `026`.

That ordering follows the current evidence:

- the refreshed `022` inventory surfaced the native Windows blockers, and the
  reopened `025`, `028`, and `031` slices are now re-closed by validated
  implementation work
- `029` and `030` are still close to closure, but they should not move ahead of
  larger refactors until their acceptance audits settle the current branch
- `024`, `023`, and `026` are larger and should be done after the smaller
  closure audits stop moving the ground underneath them
