# Story Status Matrix

This matrix is the explicit rollout status for the simplification story set on
the integration branch `simplify-runtime-codegen-dedup`.

Status meanings:

- `Done`: the integration branch covers the current story scope well enough to
  treat the story as landed at the current evidence level
- `Partial`: one or more implementation slices are landed, but the story's
  acceptance criteria are not fully covered yet
- `Open`: the story still needs its primary implementation or closure artifact

This file tracks integrated branch state, not raw worktree branch ancestry.
Most story work happened in dedicated worktrees and was cherry-picked here, so
the original worktree branch tips intentionally remain on separate commit IDs.

| Story | Title | Status | Integration evidence | Remaining gap |
| --- | --- | --- | --- | --- |
| `021` | dedup runtime test-context plumbing and generated output naming | `Done` | `c03c6535` | closed record; follow-up naming work moved to `029` |
| `022` | refresh native Windows failure inventory and split concrete follow-ups | `Done` | `2026-04-14` full native Windows inventory in `022_windows_ci_portability_followups_inventory.md` | closed inventory story; surviving failures are now owned by `025`, `028`, and `031` |
| `023` | reduce installed public API leakage of runtime internals | `Partial` | `3a10cf87`, `bcba719a` | `context.h` normal include path is narrower, but `registry.h` and `fixture.h` still expose more internal surface than the story intends |
| `024` | unify manifest and TU-wrapper emission pipelines | `Partial` | `ea142481` | shared registration render core landed, but the broader manifest-vs-TU shell unification is not complete |
| `025` | move source transformation logic out of `GentestCodegen.cmake` | `Partial` | `1db3854b` plus `2026-04-14` Windows inventory evidence | native Windows still has nested source-inspection helper/backend failures in `gentest_codegen_incremental_dependencies` and `gentest_module_name_literal_false_match` |
| `026` | simplify test helper infrastructure and derive inventory expectations | `Partial` | `e9d17314` | count/inventory duplication was reduced, but the broader helper-driver consolidation is still open |
| `027` | separate core reporting from optional Allure and format sinks | `Done` | `14fb3a91` | Allure sink split is integrated |
| `028` | make Windows path-depth and short-root behavior robust | `Partial` | `62301539` plus `2026-04-14` Windows inventory evidence | focused deep-path slice is green, but the full matrix still hits dyndep/path-budget failures in `gentest_module_mock_additive_visibility` and `gentest_module_header_unit_import_preamble` |
| `029` | unify tool and CMake codegen output naming contracts | `Partial` | `1530eb71`, `cfaeccd2`, `4818e6c2`, `e9fbeade`, `6c99aa50`, `2580562c` | major output-contract slices landed, but the story still needs an explicit final audit that no logical filename family is derived independently in both layers |
| `030` | simplify mock codegen model, discovery, and render helpers | `Partial` | `ec0a4e70`, `92982704`, `b8fe671f`, `b2519282` | shared discovery, qualifier normalization, and part of the render cleanup are integrated; the remaining gap is an explicit acceptance audit plus any still-missing normalized parameter-state or shared dispatch cleanup |
| `031` | preserve explicit scan-deps mode in installed public-module consumers | `Partial` | `7bcfc833` plus `2026-04-14` Windows inventory evidence | prior focused slice landed, but the full matrix still reopens `gentest_codegen_public_module_imports` from the installed consumer path |
