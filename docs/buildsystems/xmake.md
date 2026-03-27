# Xmake

This Xmake support is repo-local today. The helper layer in
[`xmake/gentest.lua`](../../xmake/gentest.lua) is usable inside this repository,
but it is not packaged or versioned as a downstream Xmake package yet.

## Current repo-local helper API

After `includes("xmake/gentest.lua")`, the repo-local surface is:

- `gentest_configure({...})`
- `gentest_add_mocks({...})`
- `gentest_attach_codegen({...})`

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
- optional:
  - `deps`
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
  - `includes`

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

The module slice uses the actual helper API, not a hard-coded one-off path.
The validated checked-in path uses Clang toolchains, including on Windows.

## Module example

The current checked-in module path looks like this:

```lua
target("gentest_consumer_module_mocks_xmake")
    set_kind("static")
    gentest_add_mocks({
        name = "gentest_consumer_module_mocks_xmake",
        kind = "modules",
        defs = {"tests/consumer/service_module.cppm", "tests/consumer/module_mock_defs.cppm"},
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

Build `gentest_codegen` with CMake first and point Xmake at it explicitly:

```bash
cmake --preset=host-codegen
cmake --build --preset=host-codegen --parallel

export GENTEST_CODEGEN="$PWD/build/host-codegen/tools/gentest_codegen"
xmake f -c -y -m release -o build/xmake
xmake b -a -y
```

If `GENTEST_CODEGEN` is not set, the helper falls back to a repo-local CMake
bootstrap under `build/xmake-codegen/<host>/<arch>`.

For fuller consumer validation, run the built binaries directly and exercise the
mock/bench/jitter surface:

```bash
consumer_textual_bin="$(find build/xmake -type f -name 'gentest_consumer_textual_xmake' | head -n 1)"
consumer_module_bin="$(find build/xmake -type f -name 'gentest_consumer_module_xmake' | head -n 1)"

"${consumer_textual_bin}" --run=consumer/consumer/module_mock --kind=test
"${consumer_textual_bin}" --run=consumer/consumer/module_bench --kind=bench
"${consumer_textual_bin}" --run=consumer/consumer/module_jitter --kind=jitter

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
- `tu_0000_suite_0000.module.gentest.cppm`
- `tu_0000_suite_0000.gentest.h`
- mock metadata JSON alongside the generated mock surface

## Limitations

- Repo-local only. There is no install/export or packaged Xmake integration yet.
- The helper API currently hard-wires the external module mappings for
  `gentest`, `gentest.mock`, and `gentest.bench_util`.
- There is no public option yet for extra external module mappings or custom
  clang/codegen arguments.
- The checked-in targets are the validated surface today; treat broader
  downstream use as follow-up work, not a finished product feature.
