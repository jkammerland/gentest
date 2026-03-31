# Xmake

This Xmake support is repo-local today. The helper layer in
[`xmake/gentest.lua`](../../xmake/gentest.lua) is usable inside this repository,
but it is not packaged or versioned as a downstream Xmake package yet.

For host Clang, sysroot, and cross-build configuration guidance, see
[host_toolchain_sysroots.md](host_toolchain_sysroots.md).

## Current repo-local helper API

After `includes("xmake/gentest.lua")`, the repo-local surface is:

- `gentest_configure({...})`
- `gentest_add_mocks({...})`
- `gentest_attach_codegen({...})`

The contract is the same 2-step model as the main story docs:

1. `gentest_add_mocks({...})`
2. `gentest_attach_codegen({...})`

`kind` is explicit in both calls. There is no auto-detection and no separate
public mock-linking API.

Current `gentest_add_mocks({...})` options:

- required:
  - `name`
  - `kind = "textual" | "modules"`
  - `defs`
  - `output_dir`
- textual-only:
  - `header_name`
- modules-only:
  - `module_name`
  - `defs_modules`
- optional:
  - `deps`
  - `defines`
  - `clang_args`
  - `headerfiles`
  - `target_id`

Current `gentest_attach_codegen({...})` options:

- required:
  - `name`
  - `kind = "textual" | "modules"`
  - `source`
  - `output_dir`
- optional:
  - `deps`
  - `main`
  - `defines`
  - `clang_args`
  - `includes`

`defines` and `clang_args` are forwarded to both the final Xmake compile and
the `gentest_codegen` invocation.

`gentest_configure({...})` also accepts a codegen tool block:

```lua
gentest_configure({
    project_root = os.projectdir(),
    incdirs = {"include", "tests", "third_party/include"},
    gentest_common_defines = {"FMT_HEADER_ONLY"},
    gentest_common_cxxflags = {"-Wno-attributes"},
    codegen = {
        exe = "/opt/gentest/bin/gentest_codegen",
        clang = "/opt/llvm/bin/clang++",
        scan_deps = "/opt/llvm/bin/clang-scan-deps",
    },
})
```

`codegen.clang` and `codegen.scan_deps` select the host-side Clang tools used
by `gentest_codegen`. They are separate from Xmake's target `cc` / `cxx`
selection. For module targets, the final Xmake target toolchain still needs
Clang for compilation, but that is no longer the primary codegen-host contract.

If the `codegen` table is omitted, the helper still honors these env fallbacks:

- `GENTEST_CODEGEN`
- `GENTEST_CODEGEN_HOST_CLANG`
- `GENTEST_CODEGEN_CLANG_SCAN_DEPS`

If no explicit host-Clang knob is provided, the helper keeps the old
clang-like-target fallback where possible for compatibility.

## Current repo-local targets

The checked-in [`xmake.lua`](../../xmake.lua) wires:

- classic suites:
  - `gentest_unit_xmake`
  - `gentest_integration_xmake`
  - `gentest_fixtures_xmake`
  - `gentest_skiponly_xmake`
- textual explicit mocks:
  - `gentest_consumer_textual_mocks_xmake`
  - `gentest_consumer_textual_xmake`
- named-module explicit mocks:
  - `gentest_consumer_module_mocks_xmake`
  - `gentest_consumer_module_xmake`

The module slice uses the same native helper API as the textual path, not a
hard-coded one-off route. The checked-in smoke coverage now builds the explicit
mock targets directly, verifies the generated mock/codegen artifacts, and runs
the consumer test/mock/bench/jitter surface. That path is validated on Linux
and macOS. The helper also carries Windows-specific Clang handling, but the
Windows Xmake package/bootstrap path remains more host-sensitive than the
Linux/macOS lane.

## Module example

The current checked-in module path looks like this:

```lua
target("gentest_consumer_module_mocks_xmake")
    set_kind("static")
    gentest_add_mocks({
        name = "gentest_consumer_module_mocks_xmake",
        kind = "modules",
        defs = {"tests/consumer/service_module.cppm", "tests/consumer/module_mock_defs.cppm"},
        defs_modules = {"gentest.consumer_service", "gentest.consumer_mock_defs"},
        headerfiles = {"tests/consumer/service_module.cppm", "tests/consumer/module_mock_defs.cppm"},
        module_name = "gentest.consumer_mocks",
        output_dir = path.join(current_gen_root(), "consumer_module_mocks"),
        deps = {"gentest"},
        target_id = "consumer_module_mocks",
    })

target("gentest_consumer_module_xmake")
    set_kind("binary")
    gentest_attach_codegen({
        name = "gentest_consumer_module_xmake",
        kind = "modules",
        source = "tests/consumer/cases.cppm",
        main = "tests/consumer/main.cpp",
        output_dir = path.join(current_gen_root(), "consumer_module"),
        deps = {"gentest_main", "gentest", "gentest_consumer_module_mocks_xmake"},
        defines = {"GENTEST_CONSUMER_USE_MODULES=1"},
    })
```

## Build and run

Build `gentest_codegen` with CMake first and point Xmake at the host tools
explicitly:

```bash
cmake --preset=host-codegen
cmake --build --preset=host-codegen --parallel

export GENTEST_CODEGEN="$PWD/build/host-codegen/tools/gentest_codegen"
export GENTEST_CODEGEN_HOST_CLANG=/opt/llvm/bin/clang++
export GENTEST_CODEGEN_CLANG_SCAN_DEPS=/opt/llvm/bin/clang-scan-deps
```

Textual consumers can keep the final Xmake toolchain non-Clang:

```bash
xmake f -c -y -m release -o build/xmake-textual --cc=gcc --cxx=g++
xmake b gentest_consumer_textual_xmake
```

Module consumers still need a Clang target toolchain in Xmake, but the host
Clang used by `gentest_codegen` is now configured separately:

```bash
xmake f -c -y -m release -o build/xmake-modules \
  --toolchain=llvm \
  --cc=/opt/llvm/bin/clang \
  --cxx=/opt/llvm/bin/clang++
xmake b gentest_consumer_module_xmake
```

On Fedora-style hosts, prefer the real host `clang{,++}` binaries for
`GENTEST_CODEGEN_HOST_CLANG` rather than wrapper paths.

If `GENTEST_CODEGEN` is not set, the helper falls back to a repo-local CMake
bootstrap under `build/xmake-codegen/<host>/<arch>`.

When `GENTEST_CODEGEN` points at a CMake build tree, the helper also picks up
the adjacent `compile_commands.json` automatically for module codegen. The
final Xmake targets still resolve `fmt` through Xmake's own
`add_packages("fmt")` surface; the adjacent CMake build is only reused for the
generator binary and its compile-database context.

One local caveat: the checked-in module CMake smoke test skips installed Xmake
versions older than `3.0.6`. The helper API is still the same, but the local
regression treats `3.0.6+` as the minimum supported enough for the module
smoke lane.

The checked-in consumer smoke already exercises the plain test, mock, bench,
and jitter surface after the mock targets are built. For manual local runs, use
the built binaries directly:

```bash
consumer_textual_bin="$(find build/xmake -type f -name 'gentest_consumer_textual_xmake' | head -n 1)"
consumer_module_bin="$(find build/xmake -type f -name 'gentest_consumer_module_xmake' | head -n 1)"

"${consumer_textual_bin}" --run=consumer/consumer/module_test --kind=test
"${consumer_textual_bin}" --run=consumer/consumer/module_mock --kind=test
"${consumer_textual_bin}" --run=consumer/consumer/module_bench --kind=bench
"${consumer_textual_bin}" --run=consumer/consumer/module_jitter --kind=jitter

"${consumer_module_bin}" --run=consumer/consumer/module_test --kind=test
"${consumer_module_bin}" --run=consumer/consumer/module_mock --kind=test
"${consumer_module_bin}" --run=consumer/consumer/module_bench --kind=bench
"${consumer_module_bin}" --run=consumer/consumer/module_jitter --kind=jitter
```

## Generated outputs

Classic suites generate per-TU wrappers under:

- `build/xmake/gen/<plat>/<arch>/<mode>/<suite>/tu_0000_cases.gentest.cpp`
- `build/xmake/gen/<plat>/<arch>/<mode>/<suite>/tu_0000_cases.gentest.h`

The module slice adds:

- `build/xmake/gen/<plat>/<arch>/<mode>/consumer_module_mocks/...`
- `build/xmake/gen/<plat>/<arch>/<mode>/consumer_module/...`

Notable generated module files are:

- generated mock module wrapper `.cppm` files
- `gentest/consumer_mocks.cppm`
- `tu_0000_cases.module.gentest.cppm`
- `tu_0000_cases.gentest.h`

## Limitations

- Repo-local only. There is no install/export or packaged Xmake integration yet.
- The helper API currently hard-wires the external module mappings for
  `gentest`, `gentest.mock`, and `gentest.bench_util`.
- There is still no public option for extra external module mappings.
- Module targets still require a Clang target toolchain in Xmake even though
  the codegen host Clang is now configured separately.
- The checked-in targets are the validated surface today; treat broader
  downstream use as follow-up work, not a finished product feature.
