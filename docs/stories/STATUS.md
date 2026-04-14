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
| `023` | reduce installed public API leakage of runtime internals | `Partial` | `3a10cf87`, `bcba719a`, `648975dd`, `2026-04-14` runner/detail-compat slice | `gentest/runner.h` now routes through narrow fixture/registry API layers and legacy detail users are covered by explicit compat-shim package tests, but `fixture.h`, `registry.h`, and the underlying runtime-detail helpers remain installed for compatibility |
| `024` | unify manifest and TU-wrapper emission pipelines | `Done` | `ea142481`, `2026-04-14` closure audit, `84fad185` | shared registration/body assembly is already unified; the remaining manifest-vs-TU differences are shell/orchestration level and the acceptance slice stays green |
| `025` | move source transformation logic out of `GentestCodegen.cmake` | `Done` | `1db3854b`, `2026-04-14` Windows inventory evidence, `534e241a` | reopened native Windows nested source-inspection helper/backend failures are fixed and revalidated |
| `026` | simplify test helper infrastructure and derive inventory expectations | `Partial` | `e9d17314`, `2026-04-14` inventory source-of-truth slice | audited inventory rows now derive totals and outcome counts from canonical expected-list files, with focused coverage for legacy plain lists, status markers, and semicolon-bearing names; the broader helper-driver consolidation is still open |
| `027` | separate core reporting from optional Allure and format sinks | `Done` | `14fb3a91` | Allure sink split is integrated |
| `028` | make Windows path-depth and short-root behavior robust | `Done` | `62301539`, `2026-04-14` Windows inventory evidence, `f040a658` | reopened deep-checkout path-budget failures are fixed and revalidated from a normal native Windows checkout root |
| `029` | unify tool and CMake codegen output naming contracts | `Done` | `1530eb71`, `cfaeccd2`, `4818e6c2`, `e9fbeade`, `6c99aa50`, `2580562c`, `2026-04-14` closure audit, `d520d2e7` | module-wrapper and mock-output filename families are now CMake-owned explicit paths consumed by the tool; no cross-layer filename synthesis remains in the story scope |
| `030` | simplify mock codegen model, discovery, and render helpers | `Done` | `ec0a4e70`, `92982704`, `b8fe671f`, `b2519282`, `2026-04-14` closure audit, `e62d8b1c` | current branch state already satisfies the story scope; closure evidence is the explicit audit plus the full mock-sensitive acceptance slice |
| `031` | preserve explicit scan-deps mode in installed public-module consumers | `Done` | `7bcfc833`, `2026-04-14` targeted Linux rerun, `2026-04-14` native Windows rerun, `ddb4702e` | closure slice updates the regression harness to follow the actual generated per-TU output set and broader Windows launcher forms while still asserting explicit mode propagation |
