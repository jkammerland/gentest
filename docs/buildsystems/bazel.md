# Bazel

This Bazel support is still repo-local. The macros in
[`build_defs/gentest.bzl`](../../build_defs/gentest.bzl) are usable in this
repository, but they are not packaged as a downstream rule set yet.

For host Clang, sysroot, and cross-build configuration guidance, see
[host_toolchain_sysroots.md](host_toolchain_sysroots.md).

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
  - optional: `defines`, `clang_args`, `codegen_host_clang`, `deps`, `linkopts`
- `gentest_attach_codegen_textual(...)`:
  - required: `name`, `src`, `main`, same-package `mock_targets`
  - optional: `defines`, `clang_args`, `codegen_host_clang`, `deps`, `linkopts`, `source_includes`
- `gentest_add_mocks_modules(...)`:
  - required: `name`, `defs`, `defs_modules`, `module_name`
  - optional: `defines`, `clang_args`, `codegen_host_clang`, `deps`, `linkopts`
- `gentest_attach_codegen_modules(...)`:
  - required: `name`, `src`, `main`, same-package `mock_targets`
  - optional: `defines`, `clang_args`, `codegen_host_clang`, `deps`, `linkopts`, `source_includes`

Both textual and module helpers thread `defines` / `clang_args` through the
codegen scan context and the final compile surface.

For repo-local Bazel codegen host selection, the supported contract is:

- per-target: `codegen_host_clang = "/path/to/clang++"`
- env fallback: `GENTEST_CODEGEN_HOST_CLANG=/path/to/clang++`

`CC` / `CXX` remain target-toolchain inputs. In the checked-in repo-local
smoke flow they are still mirrored from the resolved host Clang only to keep
the `//:gentest_codegen_build` CMake bootstrap on the same LLVM install.

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

The checked-in Bazel smoke coverage builds the explicit mock target and the
final consumer target for both textual and module paths, verifies the generated
mock/codegen artifacts under `bazel-bin/gen/...`, and runs the consumer
test/mock/bench/jitter surface.

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
    codegen_host_clang = "/opt/llvm/bin/clang++",
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
    codegen_host_clang = "/opt/llvm/bin/clang++",
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
host_clang_candidates=(
  /usr/lib64/llvm22/bin/clang++
  /usr/lib64/llvm21/bin/clang++
  /usr/lib64/llvm20/bin/clang++
  /usr/lib/llvm-22/bin/clang++
  /usr/lib/llvm-21/bin/clang++
  /usr/lib/llvm-20/bin/clang++
)
for host_clang in "${host_clang_candidates[@]}"; do
  if [ -x "${host_clang}" ]; then
    break
  fi
done
if [ ! -x "${host_clang}" ]; then
  host_clang="$(command -v clang++)"
fi
if [ ! -x "${host_clang}" ]; then
  echo "clang++ not found" >&2
  exit 1
fi
host_clang_dir="$(cd "$(dirname "${host_clang}")" && pwd)"
host_clang_c="${host_clang_dir}/clang"
if [ ! -x "${host_clang_c}" ]; then
  host_clang_c="$(command -v clang)"
fi
if [ ! -x "${host_clang_c}" ]; then
  echo "clang not found next to ${host_clang}" >&2
  exit 1
fi
clang_resource_dir="$("${host_clang}" -print-resource-dir)"

GENTEST_CODEGEN_HOST_CLANG="${host_clang}" \
GENTEST_CODEGEN_RESOURCE_DIR="${clang_resource_dir}" \
CC="${host_clang_c}" \
CXX="${host_clang}" \
bazelisk build \
  //:gentest_consumer_textual_bazel \
  --action_env=CCACHE_DISABLE=1 \
  --host_action_env=CCACHE_DISABLE=1 \
  --action_env=CC="${host_clang_c}" \
  --action_env=CXX="${host_clang}" \
  --action_env=GENTEST_CODEGEN_HOST_CLANG \
  --action_env=GENTEST_CODEGEN_RESOURCE_DIR="${clang_resource_dir}" \
  --host_action_env=CC="${host_clang_c}" \
  --host_action_env=CXX="${host_clang}" \
  --host_action_env=GENTEST_CODEGEN_HOST_CLANG \
  --repo_env=GENTEST_CODEGEN_HOST_CLANG \
  --host_action_env=GENTEST_CODEGEN_RESOURCE_DIR="${clang_resource_dir}"

./bazel-bin/gentest_consumer_textual_bazel --list
./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_test --kind=test
./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_mock --kind=test
./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_bench --kind=bench
./bazel-bin/gentest_consumer_textual_bazel --run=consumer/consumer/module_jitter --kind=jitter
```

Module consumer:

```bash
host_clang_candidates=(
  /usr/lib64/llvm22/bin/clang++
  /usr/lib64/llvm21/bin/clang++
  /usr/lib64/llvm20/bin/clang++
  /usr/lib/llvm-22/bin/clang++
  /usr/lib/llvm-21/bin/clang++
  /usr/lib/llvm-20/bin/clang++
)
for host_clang in "${host_clang_candidates[@]}"; do
  if [ -x "${host_clang}" ]; then
    break
  fi
done
if [ ! -x "${host_clang}" ]; then
  host_clang="$(command -v clang++)"
fi
if [ ! -x "${host_clang}" ]; then
  echo "clang++ not found" >&2
  exit 1
fi
host_clang_dir="$(cd "$(dirname "${host_clang}")" && pwd)"
host_clang_c="${host_clang_dir}/clang"
if [ ! -x "${host_clang_c}" ]; then
  host_clang_c="$(command -v clang)"
fi
if [ ! -x "${host_clang_c}" ]; then
  echo "clang not found next to ${host_clang}" >&2
  exit 1
fi
clang_resource_dir="$("${host_clang}" -print-resource-dir)"

GENTEST_CODEGEN_HOST_CLANG="${host_clang}" \
GENTEST_CODEGEN_RESOURCE_DIR="${clang_resource_dir}" \
CC="${host_clang_c}" \
CXX="${host_clang}" \
bazelisk build \
  //:gentest_consumer_module_bazel \
  --experimental_cpp_modules \
  --action_env=CCACHE_DISABLE=1 \
  --host_action_env=CCACHE_DISABLE=1 \
  --action_env=CC="${host_clang_c}" \
  --action_env=CXX="${host_clang}" \
  --action_env=GENTEST_CODEGEN_HOST_CLANG \
  --action_env=GENTEST_CODEGEN_RESOURCE_DIR="${clang_resource_dir}" \
  --host_action_env=CC="${host_clang_c}" \
  --host_action_env=CXX="${host_clang}" \
  --host_action_env=GENTEST_CODEGEN_HOST_CLANG \
  --host_action_env=GENTEST_CODEGEN_RESOURCE_DIR="${clang_resource_dir}" \
  --repo_env=CC="${host_clang_c}" \
  --repo_env=CXX="${host_clang}" \
  --repo_env=GENTEST_CODEGEN_HOST_CLANG \
  --repo_env=GENTEST_CODEGEN_RESOURCE_DIR="${clang_resource_dir}"

./bazel-bin/gentest_consumer_module_bazel --list
./bazel-bin/gentest_consumer_module_bazel --run=consumer/consumer/module_test --kind=test
./bazel-bin/gentest_consumer_module_bazel --run=consumer/consumer/module_mock --kind=test
./bazel-bin/gentest_consumer_module_bazel --run=consumer/consumer/module_bench --kind=bench
./bazel-bin/gentest_consumer_module_bazel --run=consumer/consumer/module_jitter --kind=jitter
```

In practice, the repo-local Bazel module path is still toolchain-sensitive.
Use `codegen_host_clang` or `GENTEST_CODEGEN_HOST_CLANG` for codegen-host
selection rather than relying on Bazel's default host toolchain discovery.

The checked-in Linux workflow validates:

- classic suites
- the textual Bazel consumer, including explicit mock target generation
- the module Bazel consumer under the explicit Clang + `--experimental_cpp_modules`
  contract, including explicit mock target generation and test/mock/bench/jitter execution

For local module runs, keep the same constraints:

- set `GENTEST_CODEGEN_HOST_CLANG` to the Clang executable used for codegen
- set `GENTEST_CODEGEN_RESOURCE_DIR="$(clang++ -print-resource-dir)"`
- pass `--experimental_cpp_modules`
- if you build the checked-in repo-local `//:gentest_codegen_build`, mirror the
  same LLVM install into `CC` / `CXX` as bootstrap compatibility only
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
