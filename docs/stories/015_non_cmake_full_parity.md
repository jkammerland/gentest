# Story: non-CMake full parity for modules and explicit mocks

## Goal

Build on the already-shipped classic per-TU non-CMake baseline and bring Meson,
Xmake, and Bazel to functional parity with the current CMake product model for:

- explicit mocks
- named-module test sources
- generated public mock surfaces
- test/codegen attachment for final consumer targets

Classic per-TU suite support is now a precondition for this story, not part of
the unfinished deliverable.

## Current state

Implemented already:

- Meson, Xmake, and Bazel use shared per-TU wrapper generation for the classic
  repository suites.
- Legacy `gentest_codegen --output ...` manifest mode has been removed from the
  active non-CMake suite integrations.
- Meson has an in-tree explicit textual mock consumer slice:
  - defs file -> generated public header -> consumer test target
- Bazel has the same in-tree explicit textual mock consumer slice.
- Xmake now has the same in-tree explicit textual mock consumer slice.
- The classic non-CMake paths are documented in:
  - [`docs/buildsystems/meson.md`](../buildsystems/meson.md)
  - [`docs/buildsystems/xmake.md`](../buildsystems/xmake.md)
  - [`docs/buildsystems/bazel.md`](../buildsystems/bazel.md)

Still missing:

- reusable explicit mock attachment APIs
- named-module test attachment APIs
- reusable public generated header/module mock surfaces
- module-aware handoff from mock generation into test codegen
- backend-specific plumbing for module compilation and consumption

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
7. Manifest mode stays legacy/fallback only.

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

Target outcome:

- stabilize the already-landed explicit textual mock slice into a reusable API
- keep the public API at `add_mocks(...)` + `attach_codegen(...)`
- expose the modules-shaped API now, but fail fast when `kind = modules`
- defer real named-module Meson support until the backend/toolchain path is
  reliable enough

Open work:

- expose mock metadata from `add_mocks(...)` into `attach_codegen(...)`
- avoid configure-time-only dependency snapshots for codegen-sensitive inputs
- keep module requests explicit and reject them predictably at configure/build
  time instead of attempting brittle fallback behavior
- decide whether downstream package/export support is practical in Meson or
  whether the parity line should stop at in-tree consumer parity

### Xmake

Target outcome:

- stabilize the already-landed explicit textual mock slice into reusable helper
  APIs
- support named-module test targets
- support explicit module mock targets
- keep ordering/metadata handling internal to the helper layer

Open work:

- define reusable helper functions instead of repo-local wiring only
- carry enough compile context for module-aware codegen and scan-deps
- support generated module surfaces and consumer imports
- document and test Windows-native behavior as a first-class path

### Bazel

Target outcome:

- stabilize the already-landed explicit textual mock slice into reusable rules
- support named-module test targets where Bazel toolchains can truly model the
  module graph
- support explicit module mock targets
- provide rule-level semantics instead of repo-local macros only

Open work:

- separate local convenience bootstrap from real reusable Bazel rules
- decide the acceptable hermeticity level for `gentest_codegen`
- make module support conditional on a real Bazel modules-capable toolchain path
- keep metadata handoff hermetic enough for sandboxed actions

## Execution plan

### Phase 2: explicit textual mocks

- complete the repo-local textual explicit-mock slices in all three backends
- generate a textual public header surface only
- prove the repo-local integrations can consume the generated textual surface
  without any public third linking step
- add docs and regression checks per backend
- then harden those slices toward reusable helper/public-API form

Current status:

- Meson: repo-local slice implemented
- Bazel: repo-local slice implemented
- Xmake: repo-local slice still needs stabilization

### Phase 3: named-module tests

- add `attach_codegen(kind=modules, ...)` support for named-module test sources
  per backend where the buildsystem/toolchain path is good enough
- generate/compile module wrapper units where needed
- prove final test targets can import their generated test module surfaces
- Meson is explicitly out of scope for this phase until its local named-module
  behavior is reliable enough to support as a product feature

### Phase 4: explicit module mocks

- add `add_mocks(kind=modules, ...)` support for module defs
- generate public named-module mock surfaces
- prove `attach_codegen(...)` consumes module mock metadata and final targets
  can import the generated mock surface
- Meson is explicitly out of scope for this phase until its local named-module
  behavior is reliable enough to support as a product feature

### Phase 5: downstream/reusable API polish

- stabilize public helper names/arguments
- document downstream usage
- add package/export or equivalent downstream-consumer coverage where the
  backend supports it

## Acceptance criteria

Minimum parity is reached when:

- Meson supports:
  - explicit textual mock targets
  - explicit textual test/codegen attachment
  - explicit modules-shaped APIs that fail fast with a clear diagnostic
- Xmake and Bazel each support:
  - explicit textual mock targets
  - named-module test targets
  - explicit module mock targets
- the public surface contract is the same as CMake:
  - textual defs -> header surface
  - module defs -> module surface
- the existing classic per-TU suite coverage remains green while the new
  features land
- user-facing docs exist for each backend
- CI covers at least one green path for each supported backend feature slice
- the old implicit/manifest non-CMake assumptions are removed from the docs and
  active backend code

## Non-goals

- Reintroducing implicit mock discovery.
- Auto-generating cross-surface bridges by default.
- Exposing a third public mock-linking API unless a backend truly cannot avoid
  it.
- Forcing identical implementation details across buildsystems when the public
  contract can stay the same.
