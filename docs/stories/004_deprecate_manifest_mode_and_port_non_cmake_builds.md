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

- Meson: `meson test` builds + runs all suites using per‑TU mode (no generated unity TU).
- Xmake: `xmake build` builds + runs all suites using per‑TU mode.
- Bazel: `bazel test //...` builds + runs the suites using per‑TU mode.
- Docs: manifest mode is clearly labeled “legacy/fallback”, with migration guidance.

## Notes / references

- CMake per‑TU implementation: `cmake/GentestCodegen.cmake`
- Existing manifest consumers:
  - `meson.build`
  - `xmake.lua`
  - `build_defs/gentest.bzl`
