# Story: deprecate manifest mode + port non-CMake builds to per‑TU generation

>[!NOTE]
> Phase 1 of this story is implemented: the repo-local Meson, Xmake, and Bazel
> classic suites now use per-TU generation instead of legacy manifest mode.
> Repo-local non-CMake module support has since landed too, but the remaining
> packaging/API/CI parity work is tracked in
> [`docs/stories/015_non_cmake_full_parity.md`](015_non_cmake_full_parity.md).

## Goal

This story covered phase 1 of the non-CMake migration: replacing legacy
manifest-mode codegen with per-TU generation for the repo-local classic suites.
Remaining full-parity work now lives in
[`docs/stories/015_non_cmake_full_parity.md`](015_non_cmake_full_parity.md).

Treat manifest mode (single generated unity TU via `gentest_codegen --output ...`) as legacy and migrate non‑CMake build integrations
(Bazel/Meson/Xmake) to the per‑TU registration workflow (shim TU + generated `tu_*.gentest.h`).

## Motivation / user impact

- **Avoid unity TU downsides**: worse incremental builds, reduced parallelism, higher risk of accidental ODR/link behavior differences.
- **Match “macro test frameworks” ergonomics**: each `.cpp` behaves like an ordinary test TU that self‑registers.
- **Make integrations consistent**: CMake and non‑CMake builds should share the same conceptual model and outputs.

## Background

gentest currently supports two codegen output styles:

- **Per‑TU registration mode (preferred)**: `gentest_codegen --tu-out-dir <dir> <tu_shim_0.cpp> ...`
  - Codegen emits `tu_*.gentest.h` registration headers (one per shim TU).
  - The build compiles shim TUs that `#include` the original `.cpp` and then include the generated header.
- **Manifest mode (legacy)**: `gentest_codegen --output <test_impl.cpp> <sources...>`
  - Codegen emits a single generated TU that includes sources and registers all discovered cases.

Before phase 1, the non‑CMake build files used manifest mode:
- `meson.build`
- `xmake.lua`
- `build_defs/gentest.bzl`

Current repo reality:

- CMake is still the most complete packaged/downstream surface.
- Meson, Xmake, and Bazel cover the repo-local classic/header-style suites
  through shared per-TU wrapper generation.
- Meson, Xmake, and Bazel also have repo-local textual explicit-mock slices.
- Meson now has a repo-local module consumer target, but not a reusable Meson
  helper API.
- Xmake now exposes repo-local `gentest_add_mocks(...)` and
  `gentest_attach_codegen(...)` helpers for textual and module paths.
- Bazel now exposes repo-local textual and module macros in
  [`build_defs/gentest.bzl`](../../build_defs/gentest.bzl).
- The Linux workflow still validates the classic suites and textual consumer
  slices; the module paths remain local regression coverage rather than active
  workflow lanes.

So this story is now historical context plus phase-1 design/notes. The
remaining non-CMake design story for modules and explicit mocks is
[`docs/stories/015_non_cmake_full_parity.md`](015_non_cmake_full_parity.md).

## Design principles

These integrations should mirror the CMake model at the conceptual level, even
if the syntax is different:

1. **Test/codegen integration is separate from mock generation.**
   - ordinary test targets discover tests/fixtures/cases
   - dedicated mock-def targets discover mocks

2. **Mocks are explicit only.**
   - no automatic mock discovery from arbitrary consumer sources
   - explicit defs files are the only source of truth for generated mocks

3. **Surface model is preserved.**
   - header defs stay textual and produce a generated public header surface
   - module defs stay modular and produce a generated public module surface
   - no implicit header-to-module or module-to-header bridge generation by default

4. **Consumer codegen must see generated mock targets before it executes.**
   - this is a build-graph visibility/order guarantee
   - any metadata handoff between mock generation and test codegen is an
     internal implementation detail, not a third public API

5. **Per-TU generation is the default codegen path.**
   - manifest mode remains legacy/fallback only

## Minimum non-CMake API shape

Each non-CMake buildsystem should end up with equivalents of these two
operations:

### 1) Attach test codegen

Conceptual API:

```text
attach_codegen(test_target,
               kind = textual | modules,
               sources = [...],
               output_dir = ...,
               clang_args = [...],
               deps = [...])
```

Responsibilities:

- generate shim TUs for ordinary `.cpp` inputs
- run `gentest_codegen --tu-out-dir`
- compile shim TUs instead of original classic sources
- preserve module sources as module units where the buildsystem supports them
- pass through module-aware compile arguments / compilation database context
- propagate target-local include dirs, defines, and dependency metadata into the
  codegen invocation

### 2) Add explicit mocks

Conceptual API:

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
  - produce generated public header surface
  - no generated public named module
- module defs:
  - produce generated public named module surface
  - no generated public compatibility header
- module defs must be real named module units
- `module_name` is required for module defs
- at least one authored defs module must import `gentest.mock`
- the public consumer-facing module surface is a generated aggregate module named
  by `module_name`; it re-exports `gentest`, `gentest.mock`, and the authored
  defs modules
- mixed textual + module defs in one target are rejected
- codegen runs with `--discover-mocks`
- deps / clang args flow into both mock discovery and downstream consumer metadata
- each mock target owns an exclusive concrete `output_dir`
- generated textual public paths such as `header_name` must stay within that
  target's generated tree and must not collide with reserved generated filenames

## Shared implementation model

Regardless of buildsystem, the implementation should be split into the same
phases:

1. **Source staging**
   - materialize shim TUs or staged defs into a stable build-dir location

2. **Codegen**
   - normal tests: `gentest_codegen --tu-out-dir ...`
   - explicit mocks: `gentest_codegen --discover-mocks ...`

3. **Generated surface publication**
   - textual mocks: generated public header
   - modular mocks: generated public aggregate module that re-exports
     `gentest`, `gentest.mock`, and the authored defs modules

4. **Consumer compilation**
   - compile generated shim TUs / generated module wrapper units
   - make dependent mock-target metadata visible to both consumer codegen and
     consumer compilation

## Buildsystem-specific breakdown

### Meson

Meson should get two helper layers:

- `gentest_attach_codegen(...)`
- `gentest_add_mocks(...)`

#### Meson: ordinary tests and modules

Target shape:

```meson
gentest_attach_codegen(
  'my_tests',
  kind: 'textual',
  sources: ['cases.cpp', 'helpers.cpp'],
  deps: [libgentest_runtime, libgentest_main],
)
```

Implementation:

- generate shim TUs for classic `.cpp` inputs under
  `build/<target>/gentest/tu_*.gentest.cpp`
- run `gentest_codegen --tu-out-dir <dir>` over those shim TUs
- compile shim TUs instead of the original classic sources
- for explicit `kind: 'modules'`, preserve the authored module units as codegen
  inputs but compile generated module wrapper units in the final target,
  matching the current CMake registration/attachment model
- make the generated shims first-class build outputs and first-class compile
  database inputs
- if Meson cannot yet model the required named-module graph cleanly for a case,
  fail explicitly and predictably rather than silently falling back to textual
  hacks. This is the current intended behavior for Meson `kind: 'modules'`.

#### Meson: explicit mocks

Target shape:

```meson
gentest_add_mocks(
  'service_mocks',
  kind: 'modules',
  defs: ['service.cppm', 'mock_defs.cppm'],
  output_dir: 'build/gentest/mocks/service',
  deps: [service_support],
  module_name: 'demo.service_mocks',
)
```

or textual:

```meson
gentest_add_mocks(
  'clock_mocks',
  kind: 'textual',
  defs: ['clock_mocks.hpp'],
  output_dir: 'build/gentest/mocks/clock',
  deps: [clock_support],
  header_name: 'public/clock_mocks.hpp',
)
```

Implementation:

- stage defs files into a stable build dir
- run `gentest_codegen --discover-mocks`
- create a static library/object target for the generated outputs
- publish either the generated header include dir or the generated module source
- propagate dependency include roots / compile flags into staging, codegen, and
  downstream consumer metadata

Meson-specific open point:

- decide whether the helper should be implemented as a Meson module/wrapper
  script first, or directly in `meson.build`. The simpler first step is a
  repo-local wrapper script.

### Xmake

Xmake should get two helper functions:

- `gentest_suite(...)`
- `gentest_mocks(...)`

#### Xmake: ordinary tests and modules

Target shape:

```lua
gentest_suite("my_tests", {
    sources = {"cases.cpp"},
    module_sources = {"cases.cppm"},
    deps = {"gentest_runtime", "gentest_main"},
})
```

Implementation:

- generate shim TUs in `build/gen/<target>/`
- run `gentest_codegen --tu-out-dir`
- compile shim TUs instead of original classic sources
- for module-authored tests, keep the authored module units as codegen inputs
  but compile generated module wrapper units in the final target
- pass target-local include dirs / defines / deps into both compile and codegen
  steps

#### Xmake: explicit mocks

Target shape:

```lua
gentest_mocks("service_mocks", {
    defs = {"service.cppm", "mock_defs.cppm"},
    output_dir = "build/gen/mocks/service",
    deps = {"service_support"},
    module_name = "demo.service_mocks",
})
```

or textual:

```lua
gentest_mocks("clock_mocks", {
    defs = {"clock_mocks.hpp"},
    output_dir = "build/gen/mocks/clock",
    deps = {"clock_support"},
    header_name = "public/clock_mocks.hpp",
})
```

Implementation:

- stop using only `before_buildcmd` + manifest output
- generate dedicated produced files/targets for mock surfaces
- make consumer targets depend on those generated mock targets explicitly
- make generated shim and header/module outputs participate in rebuild tracking,
  stale-file cleanup, and source deletion/rename handling

Xmake-specific open point:

- module support should be implemented only where Xmake can preserve the
  compiler’s real module compile model; do not fake module support through
  textual includes.
- keep build and test success as separate acceptance conditions; `xmake build`
  alone does not execute the suite binaries

### Bazel

Bazel needs a cleaner separation than the current `gentest_suite()` macro:

- `gentest_cc_test(...)`
- `gentest_cc_mocks(...)`

#### Bazel: ordinary tests and modules

Target shape:

```starlark
gentest_cc_test(
    name = "my_tests",
    srcs = ["cases.cpp"],
    module_srcs = ["cases.cppm"],
    deps = [":gentest_main"],
)
```

Implementation:

- replace the current repo-local `gentest_suite()` + `genrule` wiring with a
  reusable rule/provider shape
- keep emitting shim TUs + generated headers as declared outputs
- compile only those shim TUs for classic sources
- for module-authored tests, carry the original module units into codegen but
  compile generated module wrapper outputs in the final target
- inject target-local deps / compile context into the compile/codegen graph

#### Bazel: explicit mocks

Target shape:

```starlark
gentest_cc_mocks(
    name = "service_mocks",
    defs = ["service.cppm", "mock_defs.cppm"],
    output_dir = "gentest/mocks/service",
    deps = [":service_support"],
    module_name = "demo.service_mocks",
)
```

or textual:

```starlark
gentest_cc_mocks(
    name = "clock_mocks",
    defs = ["clock_mocks.hpp"],
    output_dir = "gentest/mocks/clock",
    deps = [":clock_support"],
    header_name = "public/clock_mocks.hpp",
)
```

Implementation:

- move away from plain `genrule` for mock generation
- use a dedicated rule/provider so downstream targets receive:
  - generated files
  - include roots
  - module metadata
  - codegen ordering information

Bazel-specific open point:

- named-module support should be gated on a real Bazel C++ modules path for the
  active toolchain. If that remains too weak, header/textual explicit mocks can
  still land first, but module mocks should stay explicitly unsupported rather
  than half-working.
- phase 1 Bazel support is explicitly classic-only; fixed-compdb/manual-arg
  fallback is not a credible named-module story
- decide early whether phase-1 Bazel remains local/non-hermetic or whether the
  rule design must become hermetic up front

## Historical migration order

The smallest sane order was:

1. Completed in phase 1: port Meson/Xmake/Bazel from manifest mode to per-TU
   generation for classic `.cpp` tests.
2. Deferred to [`015_non_cmake_full_parity.md`](015_non_cmake_full_parity.md):
   explicit textual mock targets.
3. Deferred to [`015_non_cmake_full_parity.md`](015_non_cmake_full_parity.md):
   consumer-side mock-link ordering.
4. Deferred to [`015_non_cmake_full_parity.md`](015_non_cmake_full_parity.md):
   named-module test-source support.
5. Deferred to [`015_non_cmake_full_parity.md`](015_non_cmake_full_parity.md):
   explicit module mock targets.
6. Deferred to [`015_non_cmake_full_parity.md`](015_non_cmake_full_parity.md):
   installed/package module-mock coverage where the buildsystem can model it.

This story ended after step 1. The remaining steps moved to
[`015_non_cmake_full_parity.md`](015_non_cmake_full_parity.md).

## Historical phase-1 scope

### 1) Deprecation plan (manifest mode)

Keep manifest mode working for now, but make “per‑TU by default” the documented path:

- Docs:
  - Clearly label manifest mode as “legacy / fallback”.
  - Explain when it’s still appropriate (multi‑config generators, bootstrapping, very constrained build graphs).
- Tooling:
  - Add a clear warning when `--output` is used (and/or when CMake `gentest_attach_codegen(... OUTPUT ...)` is used),
    pointing to per‑TU mode.
  - Provide a suppress flag/env var if needed for CI logs once the migration is underway.

### 2) Meson phase-1 port to per‑TU mode

Implemented shape:

- Generate shim TUs in the Meson build dir with stable suite-qualified names.
- In the shipped repo-local phase-1 wiring, the generated files are written in
  the build dir as `tu_0000_<suite>_cases.gentest.cpp` and
  `tu_0000_<suite>_cases.gentest.h`.
- Run codegen with `--tu-out-dir` pointing at the same directory and pass `-DGENTEST_CODEGEN=1` after `--`.
- Compile the shim TUs (not the original `.cpp`) into the test executable.

Pseudo-code (Meson conceptually, matching the phase-1 repo-local shape):

```meson
shim_dir = meson.current_build_dir()
tu_shim = join_paths(shim_dir, 'tu_0000_' + s + '_cases.gentest.cpp')

# 1) emit shim TU (custom_target or configure_file)
# 2) run gentest_codegen --tu-out-dir shim_dir tu_shim -- -DGENTEST_CODEGEN=1 ...
# 3) build exe with tu_shim (and main.cpp) instead of cases.cpp
```

### 3) Xmake phase-1 port to per‑TU mode

Implemented shape:

- In `before_build`, write shim TU(s) using `io.writefile`.
- Run `gentest_codegen --tu-out-dir <dir> <shim...> -- -DGENTEST_CODEGEN=1 ...`.
- Replace `cases.cpp` compilation with the shim TU(s) in `add_files(...)`.

Pseudo-code (Xmake conceptually):

```lua
local shim_cpp = path.join(out_dir, "tu_0000_cases.gentest.cpp")
io.writefile(shim_cpp, [[
// auto-generated
#include "tests/unit/cases.cpp"
#ifndef GENTEST_CODEGEN
#include "tu_0000_cases.gentest.h"
#endif
]])
os.execv(codegen, {"--tu-out-dir", out_dir, shim_cpp, "--", "-DGENTEST_CODEGEN=1", ...})
```

### 4) Bazel phase-1 port to per‑TU mode

Implemented shape (repo-local Starlark macro/rule):

- The shipped phase-1 repo-local path still uses `gentest_suite(...)`.
- That macro emits shim TUs with stable names as rule outputs.
- It runs `gentest_codegen --tu-out-dir` on those shims.
- It compiles only the shim(s) into the `cc_test` for classic sources.
- The next cleanup step is to replace that repo-local macro shape with reusable
  rule/provider semantics, not to redo the per-TU migration itself.

Notes:
- Bazel users may not have a `compile_commands.json`; per‑TU mode should still work by passing the needed `-- ...` clang args
  (includes/defines/standard) and relying on the fixed compilation database fallback.
- The Bazel story should decide how strict to be about hermeticity vs. convenience, and whether to require a compdb via a separate target/tool.

## Out of scope (for this story)

- Adding per‑TU support for multi‑config generators (separate story).
- Removing manifest mode entirely (only after all supported integrations migrate, and after a deprecation window).
- Reworking the public API or changing test semantics (registration should remain behaviorally identical).

## Historical acceptance notes

### Phase 1: classic per-TU parity (completed)

- Meson builds and runs the currently supported classic suite set using per-TU
  generation with no generated unity TU.
- Xmake builds the currently supported classic suite set using per-TU generation,
  and the expected suite binaries run successfully.
- Bazel tests the explicitly supported classic target set using per-TU
  generation; do not require `bazel test //...`.
- Each frontend proves:
  - original classic sources are not compiled in parallel with the generated shims
  - generated shims are first-class build outputs
  - editing one supported suite only regenerates/rebuilds that suite

The later parity phases that originally followed phase 1 now live in
[`015_non_cmake_full_parity.md`](015_non_cmake_full_parity.md):

- explicit textual mocks
- named-module test support
- explicit module mocks
- downstream/install-export parity

### Documentation

- Docs label manifest mode as legacy/fallback with migration guidance.

## Notes / references

- CMake per‑TU implementation: `cmake/GentestCodegen.cmake`
- Former manifest consumers, now phase-1 per-TU classic integrations:
  - `meson.build`
  - `xmake.lua`
  - `build_defs/gentest.bzl`
