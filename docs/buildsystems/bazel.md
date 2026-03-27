# Bazel

This Bazel support is still repo-local. The macros in
[`build_defs/gentest.bzl`](../../build_defs/gentest.bzl) are usable in this
repository, but they are not packaged as a downstream rule set yet.

## Current repo-local macro surface

Textual/classic support:

- `gentest_suite(name)`
- `gentest_add_mocks_textual(...)`
- `gentest_attach_codegen_textual(...)`

Named-module support:

- `gentest_add_mocks_modules(...)`
- `gentest_attach_codegen_modules(...)`

The current repo-local module macros are explicit rather than inferred.

`gentest_add_mocks_modules(...)` currently requires:

- `name`
- `defs`
- `module_name`
- `defs_modules`
- `generated_module_wrappers`
- `generated_module_headers`

`gentest_attach_codegen_modules(...)` currently requires:

- `name`
- `src`
- `main`
- same-package `mock_targets`

Both module macros also accept repo-local tuning knobs such as
`external_module_sources`, `deps`, `linkopts`, and `source_includes`.

## Current repo-local targets

The checked-in [`BUILD.bazel`](../../BUILD.bazel) wires:

- classic suites:
  - `gentest_unit_bazel`
  - `gentest_integration_bazel`
  - `gentest_fixtures_bazel`
  - `gentest_skiponly_bazel`
- textual explicit mocks:
  - `gentest_consumer_textual_mocks`
  - `gentest_consumer_textual_bazel`
- named-module explicit mocks:
  - `gentest_consumer_module_mocks`
  - `gentest_consumer_module_bazel`

The repo root also publishes the public module carrier libraries used by the
module macros:

- `gentest`
- `gentest_mock`
- `gentest_bench_util`

## Module example

The checked-in repo-local module path looks like this:

```python
gentest_add_mocks_modules(
    name = "gentest_consumer_module_mocks",
    defs = [
        "tests/consumer/service_module.cppm",
        "tests/consumer/module_mock_defs.cppm",
    ],
    module_name = "gentest.consumer_mocks",
    defs_modules = [
        "gentest.consumer_service",
        "gentest.consumer_mock_defs",
    ],
    generated_module_wrappers = [
        "tu_0000_def_0000_service_module.module.gentest.cppm",
        "tu_0001_def_0001_module__11e4b565.module.gentest.cppm",
    ],
    generated_module_headers = [
        "tu_0000_def_0000_service_module.gentest.h",
        "tu_0001_def_0001_module__11e4b565.gentest.h",
    ],
)

gentest_attach_codegen_modules(
    name = "gentest_consumer_module_bazel",
    src = "tests/consumer/cases.cppm",
    main = "tests/consumer/main.cpp",
    mock_targets = [":gentest_consumer_module_mocks"],
    deps = [
        ":gentest",
        ":gentest_bench_util",
    ],
    defines = ["GENTEST_CONSUMER_USE_MODULES=1"],
    source_includes = ["tests", "tests/consumer"],
)
```

## Build and run

Classic suites:

```bash
bazelisk test \
  //:gentest_unit_bazel \
  //:gentest_integration_bazel \
  //:gentest_fixtures_bazel \
  //:gentest_skiponly_bazel
```

Textual consumer:

```bash
bazelisk build //:gentest_consumer_textual_bazel
./bazel-bin/gentest_consumer_textual_bazel
```

Module consumer:

```bash
bazelisk build //:gentest_consumer_module_bazel
./bazel-bin/gentest_consumer_module_bazel
```

In practice, the repo-local Bazel path is still toolchain-sensitive. The Linux
workflow currently validates the classic suites and the textual consumer only.
For local module runs, using the same Clang-oriented environment as the textual
lane is the safest path:

- set `CC` / `CXX` to the Clang toolchain Bazel should use
- set `GENTEST_CODEGEN_RESOURCE_DIR="$(clang++ -print-resource-dir)"`
- disable ccache inside Bazel actions when needed

## Generated outputs

The module macros generate repo-local artifacts under `gen/<name>/`, including:

- staged defs/support files
- generated mock module wrappers
- generated module wrapper headers
- a generated aggregate public module
- mock metadata JSON
- a repo-local `compile_commands.json` used to give `gentest_codegen` a module
  scan context inside the Bazel action

The generated test wrapper target also emits:

- `gen/<name>/suite_0000.cppm`
- `gen/<name>/tu_0000_suite_0000.module.gentest.cppm`
- `gen/<name>/tu_0000_suite_0000.gentest.h`

## Limitations

- Repo-local only. There is no packaged downstream Bazel rule set yet.
- `gentest_add_mocks_textual(...)` currently accepts exactly one defs file.
- `gentest_attach_codegen_textual(...)` and
  `gentest_attach_codegen_modules(...)` currently require same-package
  `mock_targets`.
- `gentest_add_mocks_modules(...)` currently requires callers to spell out
  `defs_modules`, `generated_module_wrappers`, and `generated_module_headers`
  explicitly.
- The codegen bootstrap is intentionally non-hermetic today: Bazel shells out
  to CMake to build `gentest_codegen`.
- The checked-in Linux workflow does not run the Bazel module target yet, so
  treat the module path as repo-local/toolchain-sensitive rather than a
  polished downstream contract.
