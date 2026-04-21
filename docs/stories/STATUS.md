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
| `015` | non-CMake parity for explicit mocks and supported module paths | `Open` | repo-local Meson textual, Xmake textual/module, and Bazel textual/module helper paths exist; direct contract checks cover current artifact-manifest usage | packaged/downstream helper surfaces, hermetic Bazel bootstrap, broader module/mock workflow coverage, and install/export-quality non-CMake parity remain unfinished |
| `021` | dedup runtime test-context plumbing and generated output naming | `Done` | `c03c6535` | closed record; follow-up naming work moved to `029` |
| `022` | refresh native Windows failure inventory and split concrete follow-ups | `Done` | `2026-04-14` full native Windows inventory in `022_windows_ci_portability_followups_inventory.md` | closed inventory story; surviving failures are now owned by `025`, `028`, and `031` |
| `023` | reduce installed public API leakage of runtime internals | `Done` | `3a10cf87`, `bcba719a`, `648975dd`, populated `023_public_api_internal_surface_inventory.md`, `2026-04-14` runner/detail-compat slice, `2026-04-14` follow-up split to `032` | closed at current public-surface scope; inventory/classification artifact is populated and the remaining generated-code/devkit boundary cleanup moved to `032` |
| `024` | unify manifest and TU-wrapper emission pipelines | `Done` | `ea142481`, `2026-04-14` closure audit, `84fad185` | shared registration/body assembly is already unified; the remaining manifest-vs-TU differences are shell/orchestration level and the acceptance slice stays green |
| `025` | move source transformation logic out of `GentestCodegen.cmake` | `Done` | `1db3854b`, `2026-04-14` Windows inventory evidence, `534e241a` | reopened native Windows nested source-inspection helper/backend failures are fixed and revalidated |
| `026` | simplify test helper infrastructure and derive inventory expectations | `Done` | `e9d17314`, `2026-04-14` inventory source-of-truth slice, `2026-04-14` helper-driver consolidation slice | closed at current scope: audited inventory rows derive outcomes from canonical expected lists and normal run/assert helpers now share one contract driver |
| `027` | separate core reporting from optional Allure and format sinks | `Done` | `14fb3a91` | Allure sink split is integrated |
| `028` | make Windows path-depth and short-root behavior robust | `Done` | `62301539`, `2026-04-14` Windows inventory evidence, `f040a658` | reopened deep-checkout path-budget failures are fixed and revalidated from a normal native Windows checkout root |
| `029` | unify tool and CMake codegen output naming contracts | `Done` | `1530eb71`, `cfaeccd2`, `4818e6c2`, `e9fbeade`, `6c99aa50`, `2580562c`, `2026-04-14` closure audit, `d520d2e7` | module-wrapper and mock-output filename families are now CMake-owned explicit paths consumed by the tool; no cross-layer filename synthesis remains in the story scope |
| `030` | simplify mock codegen model, discovery, and render helpers | `Done` | `ec0a4e70`, `92982704`, `b8fe671f`, `b2519282`, `2026-04-14` closure audit, `e62d8b1c` | current branch state already satisfies the story scope; closure evidence is the explicit audit plus the full mock-sensitive acceptance slice |
| `031` | preserve explicit scan-deps mode in installed public-module consumers | `Done` | `7bcfc833`, `2026-04-14` targeted Linux rerun, `2026-04-14` native Windows rerun, `ddb4702e` | closure slice updates the regression harness to follow the actual generated per-TU output set and broader Windows launcher forms while still asserting explicit mode propagation |
| `032` | separate generated-code devkit boundary from installed runtime detail | `Open` | story created `2026-04-14` | generated templates, runtime-internal generated-code support includes, and legacy compatibility shims still depend on the broad installed `fixture_runtime` / `registry_runtime` layer |
| `033` | split `GentestCodegen.cmake` into focused internal modules | `Open` | story created `2026-04-15` | `cmake/GentestCodegen.cmake` still mixes toolchain resolution, source-inspector orchestration, mock staging, TU-mode planning, scan-deps plumbing, and test discovery in one large integration file |
| `034` | make codegen own artifact planning and same-module registration | `Done` | story created `2026-04-17`; first slice in `b10252fd`, `a74ad0af`, `ad2f6b52`, `684a71a3`; current branch commits `0606b042` and `aa35d0c8`; textual wrapper manifest, same-module registration manifests, mock manifest split, named mock phase subcommands, manifest-declared named-module mock output domains, stable `gentest.artifact_manifest.v1` / `gentest.mock_manifest.v1` schemas, direct textual protocol invocation coverage, and non-CMake composition docs implemented on this branch | declaration-only textual registration moved to `036`; full non-CMake parity remains in `015` |
| `035` | replace module-wrapper mock injection with split mock registration | `Done` | story created from `034` closure split; `b3a66a6b` wires same-module registration to consume a split mock registration manifest from a tool-owned `inspect-mocks` phase; `2026-04-20` closure slice documents `--mock-registration-manifest` and strengthens split-phase CMake coverage | broader legacy module-wrapper source-transformation removal remains in the cleanup campaign after wrapper-mode users migrate |
| `036` | optional declaration-only textual registration | `Open` | story created from `034` closure split | standalone declaration-only textual registration needs an explicit opt-in protocol, codegen-owned eligibility diagnostics, and positive/negative regressions |
| `037` | codegen contract cleanup campaign | `Open` | story created `2026-04-21` to own the deletion schedule promised in `034` closure; opening docs slice adds root [`DEPRECATIONS.md`](../../DEPRECATIONS.md) and links it from `README.md`, `docs/index.md`, and this status matrix | wave 4 still needs scan_inspector install absence coverage and later `EXPECT_SUBSTRING` hard-error coverage; waves 1-3 (configure-time source inspector probe; lua/bazel wrapper collapse + `.in` template removal; legacy manifest mode) remain gated by the owning story closures and release window |
