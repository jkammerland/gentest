# Story: non-CMake parity for explicit mocks and supported module paths

## Goal

Build on the already-shipped classic per-TU non-CMake baseline and bring the
supported non-CMake backends to parity with the current CMake product model for:

- explicit mocks
- named-module test sources where the backend is intentionally supported
- generated public mock surfaces
- test/codegen attachment for final consumer targets

Classic per-TU suite support is now a precondition for this story, not part of
the unfinished deliverable.

## Status

Open.

This is the named follow-up for story `034`'s full non-CMake parity gap. The
checked-in repo-local backend slices are useful evidence, but this story remains
open until the documented helper surfaces and downstream/package-quality
coverage reach the acceptance criteria below.

## Current state

Implemented already:

- Meson, Xmake, and Bazel use native per-TU wrapper generation for the classic
  repository suites.
- Legacy `gentest_codegen --output ...` manifest mode has been removed from the
  active non-CMake suite integrations.
- Meson has a repo-local textual consumer target.
- Xmake has repo-local textual and module helper APIs in
  [`xmake/gentest.lua`](../../xmake/gentest.lua).
- Bazel has repo-local textual and module macros in
  [`build_defs/gentest.bzl`](../../build_defs/gentest.bzl).
- All three buildsystem guides now document the current repo-local surfaces:
  - [`docs/buildsystems/meson.md`](../buildsystems/meson.md)
  - [`docs/buildsystems/xmake.md`](../buildsystems/xmake.md)
  - [`docs/buildsystems/bazel.md`](../buildsystems/bazel.md)

Still missing for real parity:

- packaged/downstream Meson helper APIs
- install/export-quality non-CMake surfaces
- hermetic/polished Bazel codegen bootstrap
- API polish for advanced codegen options outside the checked-in repo-local use
  cases

## Product direction

The parity target should match the current explicit-mock model, not the older
implicit mock-discovery model.

Required design rules:

1. Ordinary test targets discover tests and fixtures, not mocks.
2. Mocks remain explicit-only.
3. Header defs stay header-based.
4. Module defs stay module-based.
5. Mock generation and test codegen are two separate user-facing operations.
6. Any metadata handoff between them is internal implementation detail.
7. Manifest mode is removed in `2.0.0`; non-CMake parity must use
   TU-wrapper/artifact-manifest flows.

## Public model

Each non-CMake backend should expose only these two conceptual APIs. The kind
must be explicit in the API. The implementation may validate that the provided
files really match the declared kind, but it should not auto-detect or silently
switch modes.

### 1. Add mocks

```text
add_mocks(mock_target,
          kind = textual | modules,
          defs = [...],
          output_dir = ...,
          deps = [...],
          clang_args = [...],
          header_name = ... | null,
          module_name = ... | null)
```

Contract:

- textual defs:
  - generate a public header surface
  - do not generate a public module by default
- module defs:
  - generate a public named-module surface
  - do not generate a public compatibility header by default
- mixed textual + module defs in one target are rejected
- mock discovery runs only from defs files
- `add_mocks(...)` generates everything needed for downstream test targets:
  - generated registry/impl artifacts
  - compiled mock target
  - public surface
  - internal metadata for codegen/compile handoff

### 2. Attach codegen

```text
attach_codegen(test_target,
               kind = textual | modules,
               sources = [...],
               module_sources = [...],
               output_dir = ...,
               clang_args = [...],
               deps = [...])
```

Contract:

- discovers tests/fixtures/cases from the given test sources
- never discovers mocks from ordinary test sources
- consumes metadata from any dependent mock targets listed in `deps`
- generates all wrappers / generated headers / generated module wrappers needed
  for the final test target

There is no third public `link_mocks(...)` operation in this target model for
non-CMake parity. Ordinary target deps/links are sufficient at the user level.
If a backend needs a helper for internal ordering, keep it private to the
implementation.

Current-state note:

- the shipped CMake package still exposes `gentest_link_mocks()` today
- this story is intentionally targeting a simpler non-CMake public model rather
  than requiring that helper to exist everywhere
- if CMake eventually converges on the same 2-step contract, that is follow-up
  cleanup rather than a prerequisite for this story

## Internal architecture

The internal handoff should be:

```text
defs files
  -> add_mocks()
  -> compiled mock target + public surface + metadata

test files
  -> attach_codegen()
  -> generated test wrappers
  -> final target consumes mock metadata
```

That metadata must carry enough information for `attach_codegen(...)` to feed
the right include roots or module mappings into both:

- `gentest_codegen`
- final compilation

This is the only real coupling between the two user-facing operations.

## Buildsystem breakdown

### Meson

Current repo state:

- there is still no reusable Meson helper API
- the checked-in repo supports textual consumers only
- Meson named-module targets are intentionally unsupported
- the textual path still snapshots support headers/fragments at configure time,
  so new included support files require `meson setup --reconfigure`
- the checked-in textual path now reuses adjacent CMake-fetched `fmt` headers
  only as a fallback when Meson does not resolve a system `fmt` dependency via
  `pkg-config`

Open work:

- decide whether Meson should grow a real module helper surface at all
- keep the API shape explicit even while module execution is unsupported
- if Meson module behavior becomes reliable enough later, reintroduce a real
  module target on top of the same 2-step contract

### Xmake

Current repo state:

- repo-local helper functions exist for both textual and module paths
- module mock generation and module test attachment are wired through the same
  helper surface
- the documented `GENTEST_CODEGEN=...` flow reuses an explicit adjacent
  `compile_commands.json` only on the checked-in direct path
- final Xmake compilation still resolves `fmt` through Xmake's own dependency
  surface rather than any adjacent CMake build
- mock dependency metadata now flows through native Xmake target metadata for
  the checked-in direct consumer path
- public `defines` / `clang_args` now flow into both final Xmake compilation
  and `gentest_codegen`
- the checked-in Linux workflow validates both the textual and module consumers
  through list/test/mock/bench/jitter execution under a Clang contract

Open work:

- package/version the helper layer for downstream consumers
- expose more advanced external-module mapping control without forcing users to
  patch the helper
- add broader workflow coverage beyond the checked-in repo-local targets

### Bazel

Current repo state:

- repo-local textual and module macros both exist
- the checked-in repo wires module mock and module consumer targets
- the macros now synthesize provider-backed metadata and a repo-local
  `compile_commands.json` so `gentest_codegen` can scan the module path
- the checked-in Linux workflow validates the module consumer under an explicit
  Clang + `--experimental_cpp_modules` contract, including
  test/mock/bench/jitter execution

Open work:

- turn the repo-local macros into a cleaner downstream rule surface
- make the codegen bootstrap more hermetic
- broaden module-path workflow coverage beyond the checked-in direct consumer
  lane

## Execution plan

### Phase 2: explicit textual mocks

- complete the repo-local textual explicit-mock slices in all three backends
- generate a textual public header surface only
- prove the repo-local integrations can consume the generated textual surface
  without any public third linking step
- add docs and regression checks for each backend's supported surface
- then harden those slices toward reusable helper/public-API form

Current status:

- Meson: repo-local textual slice implemented
- Xmake: repo-local textual slice implemented
- Bazel: repo-local textual slice implemented

### Phase 3: named-module tests

Status:

- Meson: no checked-in module target; modules remain intentionally unsupported
- Xmake: repo-local helper path exists
- Bazel: repo-local macro path exists

Remaining work:

- turn the supported repo-local paths into a downstream contract
- keep Meson in explicit fail-fast mode unless its module backend becomes
  reliable enough to justify a real checked-in path
- add workflow coverage for at least one supported module lane for each backend
  that intentionally supports modules

### Phase 4: explicit module mocks

Status:

- Meson: execution is intentionally unsupported for now
- Xmake: repo-local helper path exists
- Bazel: repo-local macro path exists

Remaining work:

- package/polish the supported APIs
- document downstream expectations and limits
- keep Meson docs/tests explicit about the unsupported module boundary until
  the backend status changes
- add stronger regression and workflow coverage around the supported module
  paths

### Phase 5: downstream/reusable API polish

- stabilize public helper names/arguments
- document downstream usage
- add package/export or equivalent downstream-consumer coverage where the
  backend supports it

## Acceptance criteria

Full parity is reached when:

- Meson, Xmake, and Bazel each expose a documented helper surface for the
  supported path
- textual defs still produce a header surface and module defs still produce a
  module surface
- the existing classic per-TU suite coverage remains green
- user-facing docs exist for each backend and stay honest about repo-local vs
  packaged support
- CI covers the supported module paths instead of only the classic/textual
  baseline lanes
- the remaining repo-local-only implementation quirks are either removed or
  documented as intentional product limits

## Current rough edges

The checked-in repo-local implementation is useful, but several details remain
and are intentionally documented here rather than treated as invisible debt.

### Bazel

- `mock_targets` are still same-package only on the repo-local path
- the codegen bootstrap is still repo-local and not yet hermetic or packaged as
  a downstream rule set

### Meson

- module API shape exists, but `kind=modules` intentionally fails fast rather
  than executing
- the textual path still snapshots support headers/fragments at configure time,
  so adding new support includes still requires `meson setup --reconfigure`
- there is still no reusable/published Meson helper module; the checked-in
  wiring is repo-local

### Xmake

- the helper in [`xmake/gentest.lua`](../../xmake/gentest.lua) is usable, but it
  is still a repo-local helper rather than a separately packaged integration
- the current validation is strong for the checked-in targets, but it is still
  centered on repo-local consumer shapes rather than arbitrary downstream
  project layouts

### General

- some coverage is intentionally source-shape/static-contract oriented and is
  paired with separate runtime backend smoke tests
- the non-CMake support is honest and working for the checked-in scope, but it
  is not yet install/export-quality across all backends

## Non-goals

- Reintroducing implicit mock discovery.
- Auto-generating cross-surface bridges by default.
- Exposing a third public mock-linking API unless a backend truly cannot avoid
  it.
- Forcing identical implementation details across buildsystems when the public
  contract can stay the same.
