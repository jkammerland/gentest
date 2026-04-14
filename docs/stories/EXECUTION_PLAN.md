# Simplification Execution Plan

This plan is the next-step sequence for finishing the remaining simplification
stories on the integration branch `simplify-runtime-codegen-dedup`.

It assumes the current branch state, not raw worktree ancestry:

- active story worktree commits may live on different SHAs
- `git cherry simplify-runtime-codegen-dedup <story-branch>` is the source of
  truth for whether a story slice is already integrated by patch equivalence

## Current branch truth

- `021`, `022`, and `027` are treated as done at the current evidence level
- `023`, `024`, `026`, `028`, `029`, `030`, and `031` still need
  closure work
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

### Phase 1: Reopened Windows implementation blockers

1. Top-tier Windows blocker: story `028`.
   - Story `028`:
     fix `gentest_module_mock_additive_visibility` and
     `gentest_module_header_unit_import_preamble`, then rerun those checks from
     the same normal deep checkout style used in the refreshed `022` inventory.
   - Story `025` is re-closed by the nested helper/backend repair slice that
     revalidated `gentest_codegen_incremental_dependencies` and
     `gentest_module_name_literal_false_match` on both Linux and native
     Windows.

2. Story `031`: reconcile the reopened full-matrix public-module import failure.
   - Reproduce `gentest_codegen_public_module_imports` from the installed
     consumer path used by the full Windows matrix.
   - Decide whether the current failure is a launcher-inspection mismatch, an
     output-contract mismatch, or a real dropped scan-deps-mode regression.
   - Re-close the story only after the full-matrix Windows variant is green.

### Phase 2: Closure audits after the Windows blockers

3. Story `029`: perform the final logical filename-family audit.
   - Inventory every remaining generated filename family across
     `GentestCodegen.cmake` and tool code.
   - Decide whether the story can close on current integrated state or whether
     one final implementation slice is still needed.
   - Do not declare closure on audit text alone; closure still requires the
     story's output-sensitive validation slice to stay green.

4. Story `030`: perform the final acceptance-criteria audit.
   - Compare the already integrated mock slices against the story checklist:
     normalized parameter state, shared discovery, qualifier normalization,
     shared dispatch emission.
   - Decide whether the story can close on current integrated state or whether
     one final implementation slice is still needed.
   - Do not declare closure on audit text alone; closure still requires the
     story's mock-sensitive validation slice to stay green.

### Phase 3: Larger remaining implementation after the audits

5. Story `024`: complete manifest-vs-TU emission unification.
   - Finish consolidating the remaining shared fragment assembly in `emit.cpp`.
   - Keep mode differences at the outer shell boundary only.
   - This should happen after the `029` and `030` closure audits so output and
     mock-shape contracts are stable first.

6. Story `023`: reduce the remaining installed runtime/fixture leakage.
   - Refresh and confirm `023_public_api_internal_surface_inventory.md` first.
   - Tackle `registry.h` next, then `fixture.h`.
   - Preserve downstream/package coverage on every slice.
   - Explicitly classify what remains unstable `detail` versus fully private.

7. Story `026`: finish helper-driver consolidation.
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
- `031`: the full-matrix `gentest_codegen_public_module_imports` Windows
  consumer path is green again and still proves explicit scan-deps-mode
  propagation

## Practical next move

Start with story `028` or `025`, then take the other one, then `031`.

That ordering follows the current evidence:

- the refreshed `022` inventory already surfaced higher-priority native Windows
  blockers, so those now come first
- `028` and `025` each own two concrete failures from the refreshed inventory,
  so they belong in the same top priority tier
- `031` is a concrete reopened Windows check failure with one exact failing test
- `029` and `030` are still close to closure, but they should not move ahead of
  reopened Windows failures
- `024`, `023`, and `026` are larger and should be done after the smaller
  closure audits stop moving the ground underneath them
