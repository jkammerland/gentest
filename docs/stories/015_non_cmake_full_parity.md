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

Complete for the supported non-CMake scope.

This is the named follow-up for story `034`'s full non-CMake parity gap. The
closed scope is explicit textual mocks everywhere, named-module suites and
module mocks where the backend is intentionally supported, and honest
downstream helper documentation/tests for Meson, Xmake, and Bazel.

## Current state

Implemented already:

- Meson, Xmake, and Bazel use native per-TU wrapper generation for the classic
  repository suites.
- Legacy `gentest_codegen --output ...` manifest mode has been removed from the
  active non-CMake suite integrations.
- Meson has a textual-only declarative helper in
  [`meson/textual/meson.build`](../../meson/textual/meson.build), exercised by
  the downstream wrap fixture. Its textual wrapper sources and generated mock
  public headers are emitted by `gentest_codegen`, not checked-in Meson
  templates.
- Xmake has textual and module helper APIs in
  [`xmake/gentest.lua`](../../xmake/gentest.lua), staged through the xrepo
  downstream fixture.
- Bazel has textual and module macros in
  [`build_defs/gentest.bzl`](../../build_defs/gentest.bzl), with a public
  downstream entrypoint in [`bazel/defs.bzl`](../../bazel/defs.bzl).
- All three buildsystem guides now document the current repo-local surfaces:
  - [`docs/buildsystems/meson.md`](../buildsystems/meson.md)
  - [`docs/buildsystems/xmake.md`](../buildsystems/xmake.md)
  - [`docs/buildsystems/bazel.md`](../buildsystems/bazel.md)

Documented product limits, not story blockers:

- Meson named-module support remains intentionally unsupported.
- Bazel still bootstraps the repo-local `gentest_codegen` through CMake and is
  not a prebuilt binary package.
- Xmake xrepo coverage uses a fixture-local repository, not a published xrepo
  registry entry.
- Advanced arbitrary-dependency metadata inference is intentionally limited and
  documented per backend.

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

Each non-CMake backend exposes the two conceptual operations below. Backend
spelling is intentionally native rather than identical:

- Meson uses `gentest_textual_mocks` / `gentest_textual_suites` dictionaries
  followed by `subdir('subprojects/gentest/meson/textual')`, because Meson does
  not provide user-defined functions. This surface is textual-only and rejects
  `kind = 'modules'`.
- Xmake uses `gentest_add_mocks({...})` and
  `gentest_attach_codegen({...})` with explicit `kind`.
- Bazel uses kind-specific public macros:
  `gentest_add_mocks_textual`, `gentest_attach_codegen_textual`,
  `gentest_add_mocks_modules`, and `gentest_attach_codegen_modules`.

The kind must be explicit in the API where the backend supports more than one
kind. The implementation may validate that the provided files really match the
declared kind, but it should not auto-detect or silently switch modes.

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

- a reusable textual declarative helper exists at
  [`meson/textual/meson.build`](../../meson/textual/meson.build)
- the checked-in repo supports textual consumers only
- Meson named-module targets are intentionally unsupported
- the textual path still snapshots support headers/fragments at configure time,
  so new included support files require `meson setup --reconfigure`
- the checked-in textual path now reuses adjacent CMake-fetched `fmt` headers
  only as a fallback when Meson does not resolve a system `fmt` dependency via
  `pkg-config`

Closed decision:

- Meson is textual-only for this story.
- `kind = 'modules'` fails fast rather than pretending module support exists.
- If Meson module behavior becomes reliable enough later, it should be a new
  story on top of the same explicit mock/codegen product model.

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

Documented follow-up limits:

- publish a real external xrepo registry entry if/when needed
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

Documented follow-up limits:

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

- Meson: downstream textual helper implemented
- Xmake: downstream textual helper implemented through the xrepo fixture
- Bazel: downstream textual helper implemented through the Bzlmod fixture

### Phase 3: named-module tests

Status:

- Meson: no checked-in module target; modules remain intentionally unsupported
- Xmake: helper path exists and is covered by the xrepo fixture
- Bazel: macro path exists and is covered by the Bzlmod fixture

Remaining work:

- keep Meson in explicit fail-fast mode unless its module backend becomes
  reliable enough to justify a real checked-in path
- add more workflow coverage only as backend/toolchain risk justifies it

### Phase 4: explicit module mocks

Status:

- Meson: execution is intentionally unsupported for now
- Xmake: repo-local helper path exists
- Bazel: repo-local macro path exists

Remaining work:

- keep Meson docs/tests explicit about the unsupported module boundary until
  the backend status changes
- add stronger regression and workflow coverage around the supported module
  paths

### Phase 5: downstream/reusable API polish

- Meson: declarative textual helper documented and covered by the downstream
  wrap fixture
- Xmake: helper Lua layer documented and covered by the xrepo fixture
- Bazel: public `bazel/defs.bzl` entrypoint documented and covered by the
  Bzlmod fixture

## Acceptance criteria

The supported-scope parity target is reached when:

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

Current closing state: these criteria are satisfied for the supported scope.
Known backend limits are documented in the buildsystem guides and below.

## Current rough edges

The checked-in repo-local implementation is useful, but several details remain
and are intentionally documented here rather than treated as invisible debt.

### Bazel

- `mock_targets` are still same-package only on the repo-local path
- the codegen bootstrap is still repo-local and not yet hermetic or packaged as
  a downstream rule set
- arbitrary dependency metadata is not inferred for codegen; callers must use
  explicit `mock_targets`, `source_includes`, and gentest metadata providers

### Meson

- the textual declarative helper exists, and `kind=modules` intentionally fails
  fast rather than executing
- the textual path still snapshots support headers/fragments at configure time,
  so adding new support includes still requires `meson setup --reconfigure`
- the helper is a Meson `subdir()` fragment rather than a function because
  Meson does not support user-defined functions

### Xmake

- the current validation is strong for the checked-in targets, but it is still
  centered on repo-local consumer shapes rather than arbitrary downstream
  project layouts
- arbitrary target public include/module metadata is not inferred for codegen;
  callers should pass explicit gentest deps/metadata or codegen arguments

### General

- some coverage is intentionally source-shape/static-contract oriented and is
  paired with separate runtime backend smoke tests
- the non-CMake support is honest and working for the documented scope; broader
  package-manager publication and hermetic bootstrap work are follow-ups

## Non-goals

- Reintroducing implicit mock discovery.
- Auto-generating cross-surface bridges by default.
- Exposing a third public mock-linking API unless a backend truly cannot avoid
  it.
- Forcing identical implementation details across buildsystems when the public
  contract can stay the same.
