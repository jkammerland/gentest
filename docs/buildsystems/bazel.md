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
- `gentest_codegen_toolchain(...)`

The contract is explicit 2-step codegen:

1. add mocks
2. attach suite codegen

For module suites, `gentest_attach_codegen_modules(...)` now keeps the authored
`.cppm` as a module interface and adds a generated ordinary importer
registration source. The rule also materializes a
`<target>.artifact_manifest.json` product that records the generated
registration source, generated header, owning source, module name, and compile
context. Bazel still predeclares those outputs during analysis; the manifest is
a validation/product artifact rather than a way to create new outputs at action
execution time.

## Exec toolchain contract

Gentest code generation resolves its parser and generator from an
**exec-platform** `gentest_codegen_toolchain`; it does not take a compiler from
the target C++ toolchain, `PATH`, or the action environment. This keeps a
host-built code generator out of target dependencies when cross-compiling.

Define an implementation in a tool package and register the enclosing
`toolchain` from your root `MODULE.bazel` (or `.bazelrc`):

```python
# tools/gentest_codegen/BUILD.bazel
load("@gentest//bazel:defs.bzl", "gentest_codegen_toolchain")

gentest_codegen_toolchain(
    name = "impl",
    exec_os = "linux",
    codegen = "@gentest_tool_bundle//:gentest_codegen",
    clang = "@llvm_exec_bundle//:clangxx",
    # Required by gentest_add_mocks_modules/gentest_attach_codegen_modules.
    clang_scan_deps = "@llvm_exec_bundle//:clang_scan_deps",
    # Files not already supplied by executable DefaultInfo files/runfiles.
    runtime_files = [
        "@llvm_exec_bundle//:clang_runtime_files",
        "@llvm_exec_bundle//:cxx_standard_library_files",
    ],
    # Ordered marker files located directly inside each C++ standard-library
    # include root. Gentest passes -nostdinc++ and re-adds only these declared
    # execroot paths. Include every corresponding header tree in runtime_files.
    cxx_standard_library_roots = [
        "@llvm_exec_bundle//:libcxx/include/c++/v1/gentest_root.marker",
    ],
    # Linux only: ordered markers for Clang's resource headers and every C
    # system include root. The corresponding trees remain in runtime_files.
    system_include_roots = [
        "@llvm_exec_bundle//:lib/clang/22/include/gentest_root.marker",
        "@llvm_exec_bundle//:sysroot/usr/include/gentest_root.marker",
    ],
)

toolchain(
    name = "registered",
    toolchain = ":impl",
    toolchain_type = "@gentest//bazel:gentest_codegen_toolchain_type",
    exec_compatible_with = ["@platforms//os:linux"],
)
```

```python
# MODULE.bazel
bazel_dep(name = "platforms", version = "1.0.0")
register_toolchains("//tools/gentest_codegen:registered")
```

`codegen` and `clang` are executable labels in the exec configuration;
`clang_scan_deps` is additionally required by the named-module rules. Gentest
passes their `FilesToRunProvider` objects to the relevant codegen actions,
retaining runfiles-tree layout for wrappers and packaged tools.
It also declares their files/runfiles plus `runtime_files` in every action.
Package Clang's resource directory, shared libraries, scan-deps closure, and
the complete C++ standard-library header trees. Packaged toolchains must set
`exec_os` and set `cxx_standard_library_roots` to ordered marker files located
directly in those include roots. Linux packages must also set
`system_include_roots` to ordered marker files for the Clang resource and C
system header roots. Gentest passes `-nostdinc` on Linux and re-adds only those
declared C++/system execroot paths, so ambient `/usr/local/include`,
architecture include roots, and `/usr/include` cannot enter the action.
Windows packages follow the same `-nostdinc` contract: set
`exec_os = "windows"` and declare ordered markers for the Clang resource,
MSVC/UCRT, and Windows SDK include roots, with all corresponding trees in
`runtime_files`. Ambient Visual Studio or SDK installations are never an
implicit action input.
On macOS, use `exec_os = "macos"`, package the SDK closure, and set
`macos_sdk_root`; Gentest passes
that declared exec-path as `SDKROOT` without restoring ambient `PATH` or the
client's action environment. An absolute system path or a ccache wrapper is
not remotely portable and is not an accepted substitute for this contract.
Copying `/usr/bin/clang++` alone is not a packaged AppleClang toolchain: its
resource directory remains owned by the active Xcode toolchain. Use the
local-only fallback for that host-owned driver, or package a relocatable LLVM
distribution and its declared resource/runtime closure.

The source-tree local bootstrap discovers `clang-scan-deps` beside the selected
`GENTEST_BAZEL_LOCAL_CLANG`, then through `PATH`. This is automatic for local
development. `GENTEST_BAZEL_LOCAL_CLANG_SCAN_DEPS` is only an override for
nonstandard host-tool layouts; packaged toolchains keep the explicit label
because Bazel must know the complete executable and runfiles closure.

`exec_os` controls Gentest's invocation contract; it does not constrain Bazel
toolchain resolution. Every enclosing `toolchain(...)` must set the matching
`exec_compatible_with` value: `@platforms//os:linux`,
`@platforms//os:osx`, or `@platforms//os:windows`. Otherwise Bazel can select
a packaged toolchain for the wrong execution platform before Gentest sees
`exec_os`.

The legacy `codegen_host_clang` parameter remains syntactically accepted for
source compatibility, but a nonempty value fails analysis with this migration
path. `GENTEST_CODEGEN_HOST_CLANG` is no longer read by codegen actions.

For local source-package development on non-Windows hosts only, Gentest
registers a fallback when `GENTEST_BAZEL_LOCAL_CLANG` names a local `clang++`
executable. macOS additionally requires `GENTEST_BAZEL_LOCAL_SDKROOT` so the
repository rule can declare the SDK before any codegen action runs:

```bash
GENTEST_BAZEL_LOCAL_CLANG=/opt/llvm/bin/clang++ \
  bazelisk build --repo_env=GENTEST_BAZEL_LOCAL_CLANG //:my_tests

# macOS
GENTEST_BAZEL_LOCAL_CLANG=/opt/homebrew/opt/llvm/bin/clang++ \
GENTEST_BAZEL_LOCAL_SDKROOT="$(xcrun --show-sdk-path)" \
  bazelisk build \
    --repo_env=GENTEST_BAZEL_LOCAL_CLANG \
    --repo_env=GENTEST_BAZEL_LOCAL_SDKROOT //:my_tests
```

That fallback declares a label pointing at the local executable so it is useful
for local correctness checks, but it is deliberately **not** a remotely
executable LLVM package. Gentest marks actions using this local fallback
`no-remote`, disabling remote execution and remote action-cache use, and
`no-cache`, disabling local and disk action-cache reuse as well. They are also
`no-sandbox`: the action invokes the absolute `GENTEST_BAZEL_LOCAL_CLANG` host
path so installation-relative driver configuration remains visible. On macOS
it likewise receives the absolute `GENTEST_BAZEL_LOCAL_SDKROOT` host path,
while the repository retains only declared tool/SDK markers and never
recursively traverses framework symlink cycles. Release and cache-enabled
consumers instead receive executable labels and a relative execroot SDK path
from a prepackaged toolchain with a normalized, declared SDK closure. If
neither is available, analysis reports a missing Gentest exec codegen
toolchain instead of attempting ambient compiler lookup.

The automatic fallback is intentionally unavailable on Windows. A typical
Windows `clang++.exe` and `gentest_codegen.exe` need adjacent LLVM/Clang DLLs,
which a repository-rule symlink cannot declare as a complete execution closure.
Windows consumers must register a packaged toolchain whose `runtime_files`
contain those DLLs and Clang's resource headers.

## Downstream Bzlmod example

`MODULE.bazel`:

```python
module(name = "gentest_downstream_fixture")

bazel_dep(name = "gentest", version = "1.0.0")
bazel_dep(name = "rules_cc", version = "0.2.17")

local_path_override(
    module_name = "gentest",
    path = "/abs/path/to/gentest",
)
```

`BUILD.bazel`:

```python
load("@rules_cc//cc:defs.bzl", "cc_library")
load(
    "@gentest//bazel:defs.bzl",
    "gentest_add_mocks_modules",
    "gentest_add_mocks_textual",
    "gentest_attach_codegen_modules",
    "gentest_attach_codegen_textual",
)

cc_library(
    name = "codegen_headers",
    hdrs = ["tests/dep_case_value.hpp"],
    includes = ["tests"],
)

gentest_add_mocks_textual(
    name = "gentest_downstream_textual_mocks",
    defs = ["tests/header_mock_defs.hpp"],
    public_header = "gentest_downstream_mocks.hpp",
    deps = [":codegen_headers"],
)

gentest_attach_codegen_textual(
    name = "gentest_downstream_textual",
    src = "tests/cases.cpp",
    main = "tests/main.cpp",
    source_hdrs = [
        "tests/cases.hpp",
        "tests/private_case_value.hpp",
    ],
    mock_targets = [":gentest_downstream_textual_mocks"],
    deps = [":codegen_headers"],
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
    deps = [":codegen_headers"],
)

gentest_attach_codegen_modules(
    name = "gentest_downstream_module",
    src = "tests/cases.cppm",
    main = "tests/main.cpp",
    source_hdrs = ["tests/private_case_value.hpp"],
    mock_targets = [":gentest_downstream_module_mocks"],
    deps = [
        ":codegen_headers",
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
    cases.hpp
    cases.cpp
    cases.cppm
    dep_case_value.hpp
    header_mock_defs.hpp
    module_mock_defs.cppm
    service.cppm
    private_case_value.hpp
```

For textual suites, `source_hdrs` are the suite inputs and are declared action
dependencies, not independently inferred scan slots. A header-only suite lists
only `source_hdrs`; codegen appends
`tu_0000_<stem>.header_registration.gentest.cpp`, which is the target's compiled
source. A suite with out-of-line definitions additionally passes `src`, which is
compiled directly and exactly once and must include the self-contained annotated
declaration header listed in `source_hdrs`.

For module suites, the authored `.cppm` is the `module_interfaces` input and the
generated ordinary importer is appended to `srcs`; Bazel does not stage or copy
the module owner. Named-module mock definitions remain the one compatibility
exception: they still require transformed codegen wrappers until Gentest gains
a non-transforming exported mock-provider protocol.

## Build and run

These commands assume a prepared downstream Bzlmod project with a concrete
`MODULE.bazel`. They use Gentest's source-package local fallback, so the local
Clang path must be available to the repository rule as well as the client
environment. The checked-in fixture stores `MODULE.bazel.in`; the CTest proof
configures and stages it before invoking Bazel.

The local fallback commands below are for non-Windows hosts. On Windows,
register the packaged exec toolchain described above and omit
`GENTEST_BAZEL_LOCAL_CLANG`.

```bash
export GENTEST_BAZEL_LOCAL_CLANG=/opt/llvm/bin/clang++
# On macOS also export and forward this value:
# export GENTEST_BAZEL_LOCAL_SDKROOT="$(xcrun --show-sdk-path)"

bazelisk build --repo_env=GENTEST_BAZEL_LOCAL_CLANG \
  --experimental_cpp_modules //:gentest_downstream_module
bazelisk run --repo_env=GENTEST_BAZEL_LOCAL_CLANG \
  //:gentest_downstream_textual -- --list
bazelisk run --repo_env=GENTEST_BAZEL_LOCAL_CLANG \
  //:gentest_downstream_module -- --run=downstream/bazel/mock --kind=test
```

This is a local correctness flow: its generated actions are marked
`no-remote` and `no-cache`. For cache reuse or remote execution, register a real
packaged `gentest_codegen_toolchain` instead, following the
[exec toolchain contract](#exec-toolchain-contract); do not combine that
production flow with `GENTEST_BAZEL_LOCAL_CLANG`.

To run the checked-in proof instead of a prepared downstream project:

```bash
ctest --preset=debug-system --output-on-failure -R '^gentest_bazel_bzlmod_consumer$'
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
- verifies generated mock/codegen artifacts, including module registration
  sources and artifact manifests
- runs the consumer test/mock/bench/jitter surface

## Validated platforms

CI validates the Bazel downstream path on Ubuntu 24.04 and Fedora 43. Other
platforms are expected to follow the same source-package contract, but require
an explicitly configured module-capable Clang toolchain.

## Action cache and execution portability

The codegen rules use `ctx.actions.run` with deterministic `Args`, declared
execpath inputs/outputs, declared tool closures, and an empty action
environment. They intentionally retain the complete headers and generated mock
inputs required for parsing; unlike an incremental build generator, Bazel
cannot safely narrow those inputs without a sound depfile contract.

`--source-root .` is retained because Bazel spawns the action from its execroot:
`.` is a stable action contract, not the source checkout's current directory.
Generated include literals therefore stay execroot-relative rather than leaking
an absolute checkout or output-base path.

Gentest does not expose a persistent parse-cache directory for Bazel. A mutable
cache below a sandboxed action would make the action key incomplete and defeat
remote portability. Bazel's local, disk, and remote action caches are the
supported codegen cache. A local disk-cache check can use two isolated output
bases: use `aquery` to audit argv and declared inputs, then use the second
`build --subcommands` (or an execution log) to confirm codegen does not execute.
Do not use elapsed time as the gate.

## Limitations

- This is source-package / Bzlmod support, not a prebuilt binary package.
- `gentest_add_mocks_textual(...)` currently accepts exactly one defs file.
- `gentest_attach_codegen_*` currently require same-package `mock_targets`.
- `source_hdrs` declares same-package private file paths read directly by the
  authored suite. Cross-package headers belong in `deps`, whose transitive
  `CcInfo` headers, propagated defines, and distinct
  quote/system/include/framework roots reach mock and suite codegen as well as
  the final target. Generated mock providers preserve those categories for the
  consuming suite. `source_includes` only adds search flags; it does not declare
  files.
  Every header codegen can read must be reachable through `source_hdrs`,
  `deps`, a mock provider, or Gentest's fixed support inputs. Module-name
  mappings from arbitrary dependencies are still not inferred.
- The repo-local CMake bootstrap for `@gentest//:gentest_codegen` is a local
  source-package convenience and is not remote-execution portable. A packaged
  `codegen` executable label is required for portable consumers.
- The module path remains toolchain-sensitive, but its parser comes from the
  exec toolchain contract above.
- The artifact manifest is generated and validated as a product file; Bazel
  rules must still predeclare concrete outputs before actions run.
