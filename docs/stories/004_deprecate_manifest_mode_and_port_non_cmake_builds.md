# Story: deprecate manifest mode + port non-CMake builds to per‑TU generation

## Goal

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

Today, the non‑CMake build files use manifest mode:
- `meson.build`
- `xmake.lua`
- `build_defs/gentest.bzl`

Current repo reality:

- CMake is the only integration that supports named-module test sources, explicit
  mock targets, installed public modules, and module-aware mock linking.
- Meson, Xmake, and Bazel only cover classic/header-style test suites through
  `gentest_codegen --output ...`.
- None of the non-CMake integrations currently expose an equivalent of:
  - `gentest_attach_codegen(...)`
  - `gentest_add_mocks(...)`
  - `gentest_link_mocks(...)`

So this story is not just “port per-TU mode”. It is the non-CMake design story
for modules and explicit mocks.

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
   - this is a build-graph visibility/order guarantee, not necessarily an API
     call-order rule

5. **Per-TU generation is the default codegen path.**
   - manifest mode remains legacy/fallback only

## Minimum non-CMake API shape

Each non-CMake buildsystem should end up with equivalents of these three
operations:

### 1) Attach test codegen

Conceptual API:

```text
attach_codegen(test_target,
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

### 3) Link explicit mocks

Conceptual API:

```text
link_mocks(consumer_target, mock_targets = [...])
```

Responsibilities:

- add the generated mock surface to the consumer compile graph
- guarantee the build graph so consumer codegen sees those generated mock
  surfaces before the codegen command executes
- carry the correct include/module visibility into both compile and codegen steps
- do not treat linking as sufficient by itself; consumers must still explicitly
  `#include` the generated textual mock surface or `import` the generated module
  surface

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
   - make linked mock targets visible to both consumer codegen and consumer compilation

## Buildsystem-specific breakdown

### Meson

Meson should get two helper layers:

- `gentest_codegen_suite(...)`
- `gentest_mock_target(...)`

#### Meson: ordinary tests and modules

Target shape:

```meson
gentest_codegen_suite(
  'my_tests',
  sources: ['cases.cpp', 'helpers.cpp'],
  module_sources: ['cases.cppm'],
  deps: [libgentest_runtime, libgentest_main],
)
```

Implementation:

- generate shim TUs for classic `.cpp` inputs under
  `build/<target>/gentest/tu_*.gentest.cpp`
- run `gentest_codegen --tu-out-dir <dir>` over those shim TUs
- compile shim TUs instead of the original classic sources
- for module-authored tests, preserve the authored module units as codegen inputs
  but compile generated module wrapper units in the final target, matching the
  current CMake registration/attachment model
- make the generated shims first-class build outputs and first-class compile
  database inputs
- if Meson cannot yet model the required named-module graph cleanly for a case,
  fail explicitly and predictably rather than silently falling back to textual hacks

#### Meson: explicit mocks

Target shape:

```meson
gentest_mock_target(
  'service_mocks',
  defs: ['service.cppm', 'mock_defs.cppm'],
  output_dir: 'build/gentest/mocks/service',
  deps: [service_support],
  module_name: 'demo.service_mocks',
)
```

or textual:

```meson
gentest_mock_target(
  'clock_mocks',
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

- replace the current one-shot `genrule(... --output ...)`
- create a rule that emits shim TUs + generated headers as declared outputs
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

## Migration order

The smallest sane order is:

1. Port Meson/Xmake/Bazel from manifest mode to per-TU generation for classic
   `.cpp` tests.
2. Add explicit textual mock targets for all three.
3. Add consumer-side mock-link ordering.
4. Add named-module test-source support.
5. Add explicit module mock targets.
6. Add installed/package module-mock coverage where the buildsystem can model it.

Do not try to land module mocks first while the buildsystem still uses
manifest-mode classic test generation.

## Scope (must-do)

### 1) Deprecation plan (manifest mode)

Keep manifest mode working for now, but make “per‑TU by default” the documented path:

- Docs:
  - Clearly label manifest mode as “legacy / fallback”.
  - Explain when it’s still appropriate (multi‑config generators, bootstrapping, very constrained build graphs).
- Tooling:
  - Add a clear warning when `--output` is used (and/or when CMake `gentest_attach_codegen(... OUTPUT ...)` is used),
    pointing to per‑TU mode.
  - Provide a suppress flag/env var if needed for CI logs once the migration is underway.

### 2) Port Meson integration to per‑TU mode

Implementation sketch:

- Generate shim TUs in the Meson build dir, named like `tu_0000_cases.gentest.cpp` (stable, and collision-safe when shim basenames are unique case-insensitively).
- Run codegen with `--tu-out-dir` pointing at the same directory and pass `-DGENTEST_CODEGEN=1` after `--`.
- Compile the shim TUs (not the original `.cpp`) into the test executable.

Pseudo-code (Meson conceptually):

```meson
shim_dir = join_paths(meson.current_build_dir(), 'gentest', s)
tu_shim = join_paths(shim_dir, 'tu_0000_cases.gentest.cpp')

# 1) emit shim TU (custom_target or configure_file)
# 2) run gentest_codegen --tu-out-dir shim_dir tu_shim -- -DGENTEST_CODEGEN=1 ...
# 3) build exe with tu_shim (and main.cpp) instead of cases.cpp
```

### 3) Port Xmake integration to per‑TU mode

Implementation sketch:

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

### 4) Port Bazel integration to per‑TU mode

Implementation sketch (Starlark macro/rule):

- Add a `gentest_per_tu_suite(...)` helper that:
  - Generates shim TUs with stable names (rule output).
  - Runs `gentest_codegen --tu-out-dir` on those shims.
  - Compiles only the shim(s) into the `cc_test` (original `.cpp` should not be compiled separately).
- Make the original `.cpp` available to the shim via:
  - `textual_hdrs` (or `hdrs`) so changes trigger rebuilds, and
  - a predictable include path (avoid embedding absolute paths where possible).

Notes:
- Bazel users may not have a `compile_commands.json`; per‑TU mode should still work by passing the needed `-- ...` clang args
  (includes/defines/standard) and relying on the fixed compilation database fallback.
- The Bazel story should decide how strict to be about hermeticity vs. convenience, and whether to require a compdb via a separate target/tool.

## Out of scope (for this story)

- Adding per‑TU support for multi‑config generators (separate story).
- Removing manifest mode entirely (only after all supported integrations migrate, and after a deprecation window).
- Reworking the public API or changing test semantics (registration should remain behaviorally identical).

## Acceptance criteria

### Phase 1: classic per-TU parity

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

### Phase 2: textual explicit mocks

- Meson/Xmake/Bazel each have a documented and tested explicit-mock target path
  for textual defs.
- Each frontend has at least one positive consumer regression that includes the
  generated textual mock surface and links the mock target successfully.
- Each frontend proves additive visibility for multiple linked mock targets on a
  single consumer target, or explicitly defers that support.
- Each frontend proves the consumer codegen step depends on the linked mock
  target strongly enough that the generated mock surface is visible during the
  first parse.

### Phase 3: modules

- A frontend only claims module support if it has explicit regression coverage
  for module-authored test sources.
- A frontend only claims explicit module-mock support if it has explicit
  regression coverage for:
  - module defs producing a generated module surface
  - consumer import of that generated module surface
  - no default header/module bridge generation
- If a frontend does not support modules yet, the failure mode is explicit and
  documented.

### Phase 4: downstream/install-export

- A frontend only claims downstream/exported mock-target support if it preserves
  dependency/include/module metadata strongly enough for consumer builds.
- If downstream/exported mock targets are not supported for a frontend, state
  that explicitly rather than implying parity with CMake.

### Documentation

- Docs label manifest mode as legacy/fallback with migration guidance.

## Notes / references

- CMake per‑TU implementation: `cmake/GentestCodegen.cmake`
- Existing manifest consumers:
  - `meson.build`
  - `xmake.lua`
  - `build_defs/gentest.bzl`
