# Bazel

This Bazel support is still repo-local. The macros in
[`build_defs/gentest.bzl`](../../build_defs/gentest.bzl) are usable in this
repository, but they are not packaged as a downstream rule set yet.

The checked-in repo-local targets now use the 2-step explicit model without
spelling hashed textual staged outputs or generated module wrapper/header names
in [`BUILD.bazel`](../../BUILD.bazel). Those generated-file details are handled
inside [`build_defs/gentest.bzl`](../../build_defs/gentest.bzl), not repeated
at each call site.

## Current repo-local macro surface

Textual/classic support:

- `gentest_suite(name)`
- `gentest_add_mocks_textual(...)`
- `gentest_attach_codegen_textual(...)`

Named-module support:

- `gentest_add_mocks_modules(...)`
- `gentest_attach_codegen_modules(...)`

The repo-local helpers expose the 2-step explicit model:

- `gentest_add_mocks_textual(...)`:
  - required: `name`, `defs`, `public_header`
  - optional: `defines`, `clang_args`, `deps`, `linkopts`
- `gentest_attach_codegen_textual(...)`:
  - required: `name`, `src`, `main`, same-package `mock_targets`
  - optional: `defines`, `clang_args`, `deps`, `linkopts`, `source_includes`
- `gentest_add_mocks_modules(...)`:
  - required: `name`, `defs`, `defs_modules`, `module_name`
  - optional: `defines`, `clang_args`, `deps`, `linkopts`
- `gentest_attach_codegen_modules(...)`:
  - required: `name`, `src`, `main`, same-package `mock_targets`
  - optional: `defines`, `clang_args`, `deps`, `linkopts`, `source_includes`

Both textual and module helpers thread `defines` / `clang_args` through the
codegen scan context and the final compile surface.

Those `clang_args` values are treated as literal compiler flags. They are not a
shell fragment surface, and Bazel make-variable expansion inside user-supplied
`clang_args` is intentionally not supported.

Current repo-local note:

- the textual path does not rely on an adjacent `compile_commands.json`
  alongside `gentest_codegen`
- the module path still synthesizes its own explicit module compdb inputs inside
  the repo-local macros

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

## Repo-local examples

The checked-in repo-local textual path looks like this:

```python
gentest_add_mocks_textual(
    name = "gentest_consumer_textual_mocks",
    defs = ["tests/consumer/header_mock_defs.hpp"],
    public_header = "gentest_consumer_mocks.hpp",
)

gentest_attach_codegen_textual(
    name = "gentest_consumer_textual_bazel",
    src = "tests/consumer/cases.cpp",
    main = "tests/consumer/main.cpp",
    mock_targets = [":gentest_consumer_textual_mocks"],
    source_includes = ["tests", "tests/consumer"],
)
```

The checked-in repo-local module path looks like this:

```python
gentest_add_mocks_modules(
    name = "gentest_consumer_module_mocks",
    defs = [
        "tests/consumer/service_module.cppm",
        "tests/consumer/module_mock_defs.cppm",
    ],
    module_name = "gentest.consumer_mocks",
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
clang_cxx_candidates=(
  /usr/lib64/llvm22/bin/clang++
  /usr/lib64/llvm21/bin/clang++
  /usr/lib64/llvm20/bin/clang++
  /usr/lib/llvm-22/bin/clang++
  /usr/lib/llvm-21/bin/clang++
  /usr/lib/llvm-20/bin/clang++
)
for clang_cxx in "${clang_cxx_candidates[@]}"; do
  if [ -x "${clang_cxx}" ]; then
    break
  fi
done
if [ ! -x "${clang_cxx}" ]; then
  clang_cxx="$(command -v clang++)"
fi
clang_cc_candidates=(
  /usr/lib64/llvm22/bin/clang
  /usr/lib64/llvm21/bin/clang
  /usr/lib64/llvm20/bin/clang
  /usr/lib/llvm-22/bin/clang
  /usr/lib/llvm-21/bin/clang
  /usr/lib/llvm-20/bin/clang
)
for clang_cc in "${clang_cc_candidates[@]}"; do
  if [ -x "${clang_cc}" ]; then
    break
  fi
done
if [ ! -x "${clang_cc}" ]; then
  clang_cc="$(command -v clang)"
fi
if [ ! -x "${clang_cxx}" ] || [ ! -x "${clang_cc}" ]; then
  echo "clang/clang++ not found" >&2
  exit 1
fi
clang_resource_dir="$("${clang_cxx}" -print-resource-dir)"

CC="${clang_cc}" \
CXX="${clang_cxx}" \
GENTEST_CODEGEN_RESOURCE_DIR="${clang_resource_dir}" \
bazelisk build \
  //:gentest_consumer_textual_bazel \
  --action_env=CCACHE_DISABLE=1 \
  --host_action_env=CCACHE_DISABLE=1 \
  --action_env=CC="${clang_cc}" \
  --action_env=CXX="${clang_cxx}" \
  --action_env=GENTEST_CODEGEN_RESOURCE_DIR="${clang_resource_dir}" \
  --host_action_env=CC="${clang_cc}" \
  --host_action_env=CXX="${clang_cxx}" \
  --host_action_env=GENTEST_CODEGEN_RESOURCE_DIR="${clang_resource_dir}"

./bazel-bin/gentest_consumer_textual_bazel --list
./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_test --kind=test
./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_mock --kind=test
./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_bench --kind=bench
./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_jitter --kind=jitter
```

Module consumer:

```bash
clang_cxx_candidates=(
  /usr/lib64/llvm22/bin/clang++
  /usr/lib64/llvm21/bin/clang++
  /usr/lib64/llvm20/bin/clang++
  /usr/lib/llvm-22/bin/clang++
  /usr/lib/llvm-21/bin/clang++
  /usr/lib/llvm-20/bin/clang++
)
for clang_cxx in "${clang_cxx_candidates[@]}"; do
  if [ -x "${clang_cxx}" ]; then
    break
  fi
done
if [ ! -x "${clang_cxx}" ]; then
  clang_cxx="$(command -v clang++)"
fi
clang_cc_candidates=(
  /usr/lib64/llvm22/bin/clang
  /usr/lib64/llvm21/bin/clang
  /usr/lib64/llvm20/bin/clang
  /usr/lib/llvm-22/bin/clang
  /usr/lib/llvm-21/bin/clang
  /usr/lib/llvm-20/bin/clang
)
for clang_cc in "${clang_cc_candidates[@]}"; do
  if [ -x "${clang_cc}" ]; then
    break
  fi
done
if [ ! -x "${clang_cc}" ]; then
  clang_cc="$(command -v clang)"
fi
if [ ! -x "${clang_cxx}" ] || [ ! -x "${clang_cc}" ]; then
  echo "clang/clang++ not found" >&2
  exit 1
fi
clang_resource_dir="$("${clang_cxx}" -print-resource-dir)"

CC="${clang_cc}" \
CXX="${clang_cxx}" \
GENTEST_CODEGEN_RESOURCE_DIR="${clang_resource_dir}" \
bazelisk build \
  //:gentest_consumer_module_bazel \
  --experimental_cpp_modules \
  --action_env=CCACHE_DISABLE=1 \
  --host_action_env=CCACHE_DISABLE=1 \
  --action_env=CC="${clang_cc}" \
  --action_env=CXX="${clang_cxx}" \
  --action_env=GENTEST_CODEGEN_RESOURCE_DIR="${clang_resource_dir}" \
  --host_action_env=CC="${clang_cc}" \
  --host_action_env=CXX="${clang_cxx}" \
  --host_action_env=GENTEST_CODEGEN_RESOURCE_DIR="${clang_resource_dir}" \
  --repo_env=CC="${clang_cc}" \
  --repo_env=CXX="${clang_cxx}" \
  --repo_env=GENTEST_CODEGEN_RESOURCE_DIR="${clang_resource_dir}"

./bazel-bin/gentest_consumer_module_bazel --list
./bazel-bin/gentest_consumer_module_bazel --run=consumer/consumer/module_test --kind=test
./bazel-bin/gentest_consumer_module_bazel --run=consumer/consumer/module_mock --kind=test
./bazel-bin/gentest_consumer_module_bazel --run=consumer/consumer/module_bench --kind=bench
./bazel-bin/gentest_consumer_module_bazel --run=consumer/consumer/module_jitter --kind=jitter
```

In practice, the repo-local Bazel module path is still toolchain-sensitive.
Use the Clang-oriented contract above rather than relying on Bazel's default
host toolchain discovery.

The checked-in Linux workflow validates:

- classic suites
- the textual Bazel consumer
- the module Bazel consumer under the explicit Clang + `--experimental_cpp_modules`
  contract, including test/mock/bench/jitter execution

For local module runs, keep the same constraints:

- set `CC` / `CXX` to the Clang toolchain Bazel should use
- set `GENTEST_CODEGEN_RESOURCE_DIR="$(clang++ -print-resource-dir)"`
- pass `--experimental_cpp_modules`
- disable ccache inside Bazel actions when needed

## Generated outputs

The module macros generate repo-local artifacts under `gen/<name>/`, including:

- staged defs/support files
- generated mock module wrappers
- generated module wrapper headers
- a generated aggregate public module
- native `GentestGeneratedInfo` provider metadata
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
- The codegen bootstrap is intentionally non-hermetic today: Bazel shells out
  to CMake to build `gentest_codegen`.
- Extra external-module mappings are not exposed as a polished public option in
  the repo-local macros yet.
- The module path is still repo-local/toolchain-sensitive rather than a
  polished downstream contract.
