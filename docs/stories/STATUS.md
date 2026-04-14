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
| `022` | refresh native Windows failure inventory and split concrete follow-ups | `Open` | inventory updated in `022_windows_ci_portability_followups_inventory.md` | still needs a fresh full native Windows `ctest --preset=debug-system` inventory, not only the focused deep-path slice |
| `023` | reduce installed public API leakage of runtime internals | `Partial` | `3a10cf87`, `bcba719a` | `context.h` normal include path is narrower, but `registry.h` and `fixture.h` still expose more internal surface than the story intends |
| `024` | unify manifest and TU-wrapper emission pipelines | `Partial` | `ea142481` | shared registration render core landed, but the broader manifest-vs-TU shell unification is not complete |
| `025` | move source transformation logic out of `GentestCodegen.cmake` | `Done` | `1db3854b` | current migration slice is integrated and validated |
| `026` | simplify test helper infrastructure and derive inventory expectations | `Partial` | `e9d17314` | count/inventory duplication was reduced, but the broader helper-driver consolidation is still open |
| `027` | separate core reporting from optional Allure and format sinks | `Done` | `14fb3a91` | Allure sink split is integrated |
| `028` | make Windows path-depth and short-root behavior robust | `Done` | `62301539` | focused deep-path/path-budget slice is green; later contract follow-ups moved to `029` and `031` |
| `029` | unify tool and CMake codegen output naming contracts | `Partial` | `4818e6c2`, `e9fbeade`, `6c99aa50`, `2580562c` | major output-contract slices landed, but the story still needs an explicit final audit that no logical filename family is derived independently in both layers |
| `030` | simplify mock codegen model, discovery, and render helpers | `Partial` | `ec0a4e70` | only the first discovery dedup slice is integrated; broader qualifier, dispatch, and render-model simplification remains |
| `031` | preserve explicit scan-deps mode in installed public-module consumers | `Done` | `7bcfc833` | focused contract regression is green on the validated slice |

