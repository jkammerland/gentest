# Story: non-CMake full parity for modules and explicit mocks

## Goal

Build on the already-shipped classic per-TU non-CMake baseline and bring Meson,
Xmake, and Bazel to functional parity with the current CMake path for the
remaining missing pieces:

- named-module test sources
- explicit mock targets
- generated public mock surfaces
- downstream/package-style consumption where the buildsystem can support it

Classic per-TU suite support is now a precondition for this story, not part of
the unfinished deliverable.

## Current state

Implemented already:

- Meson, Xmake, and Bazel use shared per-TU wrapper generation for the classic
  repository suites.
- Legacy `gentest_codegen --output ...` manifest mode has been removed from the
  active non-CMake suite integrations.
- The classic non-CMake paths are documented in:
  - [`docs/buildsystems/meson.md`](../buildsystems/meson.md)
  - [`docs/buildsystems/xmake.md`](../buildsystems/xmake.md)
  - [`docs/buildsystems/bazel.md`](../buildsystems/bazel.md)

Still missing:

- named-module suite integration
- explicit mock target APIs
- public generated textual/module mock surfaces
- module-aware consumer ordering / dependency propagation
- downstream-facing reusable APIs with clear buildsystem contracts

## Product direction

The parity target should match the current CMake model, not the older implicit
mock-discovery model.

Required design rules:

1. Ordinary test targets discover tests and fixtures, not mocks.
2. Mocks remain explicit-only.
3. Header defs stay header-based.
4. Module defs stay module-based.
5. Linking a mock target is not enough by itself; consumers must still include
   or import the generated public surface.
6. Manifest mode stays legacy/fallback only.

## Parity target

Each non-CMake backend should end up with equivalents of these conceptual APIs:

### 1. Attach codegen

```text
attach_codegen(test_target,
               sources = [...],
               module_sources = [...],
               output_dir = ...,
               clang_args = [...],
               deps = [...])
```

Responsibilities:

- generate classic per-TU shims for ordinary `.cpp` sources
- preserve named-module units as module inputs
- generate and compile module wrapper units where needed
- propagate compile context into `gentest_codegen`
- preserve correct dependency ordering for generated sources and downstream
  targets

### 2. Add explicit mocks

```text
add_mocks(mock_target,
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

### 3. Link explicit mocks

```text
link_mocks(consumer_target, mock_targets = [...])
```

Responsibilities:

- make generated mock surfaces visible to consumer compilation
- guarantee consumer codegen runs after required mock targets are ready
- propagate include roots/module metadata into both codegen and compile steps

## Buildsystem breakdown

### Meson

Target outcome:

- support classic suites and named-module suites
- support explicit textual mock targets
- support explicit module mock targets
- expose generated module surfaces in a way Meson can compile and consume

Open work:

- replace the current repo-local suite loop with reusable helper entrypoints
- make module wrapper generation first-class
- avoid configure-time-only dependency snapshots for codegen-sensitive inputs
- decide whether downstream package/export support is practical in Meson or
  whether the parity line should stop at in-tree consumer parity

### Xmake

Target outcome:

- support classic suites and named-module suites
- support explicit textual and module mock targets
- support stable codegen ordering without relying on fragile implicit fallback
  behavior

Open work:

- define reusable helper functions instead of repo-local wiring only
- carry enough compile context for module-aware codegen and scan-deps
- support generated module surfaces and consumer imports
- document and test Windows-native behavior as a first-class path

### Bazel

Target outcome:

- support classic suites and named-module suites where Bazel toolchains can
  truly model the module graph
- support explicit textual and module mock targets
- provide rule-level semantics instead of repo-local macros only

Open work:

- separate local convenience bootstrap from real reusable Bazel rules
- decide the acceptable hermeticity level for `gentest_codegen`
- make module support conditional on a real Bazel modules-capable toolchain path
- define whether downstream parity includes install/export-style package
  consumption, or whether Bazel parity should stop at in-workspace rule parity

## Execution plan

### Phase 2: explicit mock parity for non-CMake classic consumers

- add textual explicit mock target helpers for Meson, Xmake, and Bazel
- keep the surface textual only
- prove consumer ordering and compile visibility
- add docs and regression checks per backend

### Phase 3: named-module test parity

- add named-module test support per backend
- generate/compile module wrapper units where needed
- prove public module import flows for in-tree consumers

### Phase 4: module explicit mocks

- add explicit module mock target support
- generate aggregate public mock modules
- prove module consumer imports and codegen ordering

### Phase 5: downstream/reusable API polish

- stabilize public helper names/arguments
- document downstream usage
- add package/export or equivalent downstream-consumer coverage where the
  backend supports it

## Acceptance criteria

Minimum parity is reached when:

- Meson, Xmake, and Bazel each support:
  - named-module test suites
  - explicit textual mock targets
  - explicit module mock targets
- the existing classic per-TU suite coverage remains green while the new
  features land
- the public surface contract is the same as CMake:
  - textual defs -> header surface
  - module defs -> module surface
- user-facing docs exist for each backend
- CI covers at least one green path for each supported backend feature slice
- the old implicit/manifest non-CMake assumptions are removed from the docs and
  active backend code

## Non-goals

- Reintroducing implicit mock discovery.
- Auto-generating cross-surface bridges by default.
- Forcing identical implementation details across buildsystems when the public
  contract can stay the same.
