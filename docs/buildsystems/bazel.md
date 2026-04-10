# Bazel

gentest now has an official downstream Bazel source-package surface. Downstream
users should load the public entrypoint from
[`bazel/defs.bzl`](../../bazel/defs.bzl), not the repo-private implementation in
[`build_defs/gentest.bzl`](../../build_defs/gentest.bzl).

For host Clang, sysroot, and cross-build guidance, see
[host_toolchain_sysroots.md](host_toolchain_sysroots.md).

## Public API

The public Bazel surface is:

- `gentest_suite(name)`
- `gentest_add_mocks_textual(...)`
- `gentest_attach_codegen_textual(...)`
- `gentest_add_mocks_modules(...)`
- `gentest_attach_codegen_modules(...)`
- `GentestGeneratedInfo`

The contract is explicit 2-step codegen:

1. add mocks
2. attach suite codegen

Host-tool selection is explicit:

- per target: `codegen_host_clang = "/path/to/clang++"`
- env fallback: `GENTEST_CODEGEN_HOST_CLANG=/path/to/clang++`

`CC` / `CXX` remain target-toolchain inputs. In CI they are still mirrored from
the chosen LLVM install only to keep the repo-local `gentest_codegen` bootstrap
on the same toolchain.

## Downstream Bzlmod example

`MODULE.bazel`:

```python
module(name = "gentest_downstream_fixture")

bazel_dep(name = "gentest", version = "0.0.0")

local_path_override(
    module_name = "gentest",
    path = "/abs/path/to/gentest",
)
```

`BUILD.bazel`:

```python
load(
    "@gentest//bazel:defs.bzl",
    "gentest_add_mocks_modules",
    "gentest_add_mocks_textual",
    "gentest_attach_codegen_modules",
    "gentest_attach_codegen_textual",
)

gentest_add_mocks_textual(
    name = "gentest_downstream_textual_mocks",
    defs = ["tests/header_mock_defs.hpp"],
    public_header = "gentest_downstream_mocks.hpp",
)

gentest_attach_codegen_textual(
    name = "gentest_downstream_textual",
    src = "tests/cases.cpp",
    main = "tests/main.cpp",
    mock_targets = [":gentest_downstream_textual_mocks"],
    source_includes = ["tests"],
)

gentest_add_mocks_modules(
    name = "gentest_downstream_module_mocks",
    defs = [
        "tests/service.cppm",
        "tests/module_mock_defs.cppm",
    ],
    defs_modules = [
        "downstream.bazel.service",
        "downstream.bazel.mock_defs",
    ],
    module_name = "downstream.bazel.consumer_mocks",
)

gentest_attach_codegen_modules(
    name = "gentest_downstream_module",
    src = "tests/cases.cppm",
    main = "tests/main.cpp",
    mock_targets = [":gentest_downstream_module_mocks"],
    deps = [
        "@gentest//:gentest",
        "@gentest//:gentest_bench_util",
    ],
    defines = ["GENTEST_DOWNSTREAM_USE_MODULES=1"],
    source_includes = ["tests"],
)
```

The checked-in downstream fixture uses this exact surface:

- [`tests/downstream/bazel_bzlmod_consumer/MODULE.bazel.in`](../../tests/downstream/bazel_bzlmod_consumer/MODULE.bazel.in)
- [`tests/downstream/bazel_bzlmod_consumer/BUILD.bazel`](../../tests/downstream/bazel_bzlmod_consumer/BUILD.bazel)

Minimal downstream layout:

```text
your_project/
  MODULE.bazel
  BUILD.bazel
  tests/
    main.cpp
    cases.cpp
    cases.cppm
    header_mock_defs.hpp
    module_mock_defs.cppm
    service.cppm
```

## Build and run

```bash
HOST_CLANG=/opt/llvm/bin/clang++
HOST_CC=/opt/llvm/bin/clang
RES_DIR="$($HOST_CLANG -print-resource-dir)"

GENTEST_CODEGEN_HOST_CLANG="$HOST_CLANG" \
GENTEST_CODEGEN_RESOURCE_DIR="$RES_DIR" \
CC="$HOST_CC" \
CXX="$HOST_CLANG" \
bazelisk build //:gentest_downstream_module \
  --experimental_cpp_modules \
  --action_env=CC \
  --action_env=CXX \
  --action_env=GENTEST_CODEGEN_HOST_CLANG \
  --action_env=GENTEST_CODEGEN_RESOURCE_DIR \
  --host_action_env=CC \
  --host_action_env=CXX \
  --host_action_env=GENTEST_CODEGEN_HOST_CLANG \
  --host_action_env=GENTEST_CODEGEN_RESOURCE_DIR \
  --repo_env=CC \
  --repo_env=CXX \
  --repo_env=GENTEST_CODEGEN_HOST_CLANG \
  --repo_env=GENTEST_CODEGEN_RESOURCE_DIR

GENTEST_CODEGEN_HOST_CLANG="$HOST_CLANG" \
GENTEST_CODEGEN_RESOURCE_DIR="$RES_DIR" \
bazelisk run //:gentest_downstream_textual -- --list

GENTEST_CODEGEN_HOST_CLANG="$HOST_CLANG" \
GENTEST_CODEGEN_RESOURCE_DIR="$RES_DIR" \
bazelisk run //:gentest_downstream_module -- --run=downstream/module_mock --kind=test
```

## Checked-in proofs

The repository now validates two Bazel shapes:

- repo-root examples in [`BUILD.bazel`](../../BUILD.bazel)
- downstream Bzlmod consumer in
  [`tests/downstream/bazel_bzlmod_consumer`](../../tests/downstream/bazel_bzlmod_consumer)

The downstream proof in
[`tests/cmake/scripts/CheckBazelBzlmodConsumer.cmake`](../../tests/cmake/scripts/CheckBazelBzlmodConsumer.cmake):

- builds textual mock target + textual consumer
- builds module mock target + module consumer
- resolves `bazel-bin` with `bazel info bazel-bin`
- verifies generated mock/codegen artifacts
- runs the consumer test/mock/bench/jitter surface

## Limitations

- This is source-package / Bzlmod support, not a prebuilt binary package.
- `gentest_add_mocks_textual(...)` currently accepts exactly one defs file.
- `gentest_attach_codegen_*` currently require same-package `mock_targets`.
- The repo still bootstraps `gentest_codegen` via CMake inside Bazel.
- The module path remains toolchain-sensitive and expects explicit host-tool
  configuration.
