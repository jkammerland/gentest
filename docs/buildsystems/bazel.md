# Bazel

This page shows how to use gentest in a Bazel project with Bzlmod. Load
gentest's rules from `@gentest//bazel:defs.bzl`. They run `gentest_codegen` as
Bazel actions and wrap the results in `cc_library` and `cc_test` targets. Don't
load `build_defs/gentest.bzl`; it is gentest's internal implementation.

The rules support header registration (the `*_textual` rules) and named
modules (the `*_modules` rules). In gentest's Bazel API, textual means header
registration. To compare Bazel with the other integrations, see
[Choosing a build system](downstream_consumers.md).

Codegen actions take `gentest_codegen` and the host Clang from an exec
toolchain. For local work, gentest registers a fallback toolchain that builds
`gentest_codegen` from source and uses the Clang named by
`GENTEST_BAZEL_LOCAL_CLANG`. For remote execution or cached builds, register a
packaged toolchain as described in
[Exec toolchain contract](#exec-toolchain-contract).

## Quick start

You need:

- Linux or macOS; the local fallback doesn't run on Windows;
- Bazel 9 (gentest's own `.bazelversion` pins 9.0.1);
- a gentest source checkout;
- a host Clang, CMake, and the LLVM/Clang development packages: the first
  build compiles `gentest_codegen` with CMake (see
  [Install gentest on Linux](../install/linux.md)).

`MODULE.bazel`:

```starlark
module(name = "my_tests")

bazel_dep(name = "gentest", version = "2.0.0")

local_path_override(
    module_name = "gentest",
    path = "/path/to/gentest",
)
```

`BUILD.bazel`:

```starlark
load("@gentest//bazel:defs.bzl", "gentest_attach_codegen_textual")

gentest_attach_codegen_textual(
    name = "my_tests",
    source_hdrs = ["tests/cases.hpp"],
)
```

`tests/cases.hpp`:

```cpp
#pragma once

#include "gentest/test.h"

using namespace gentest::asserts;

[[using gentest: test("demo/addition")]]
inline void addition() {
    EXPECT_EQ(2 + 2, 4);
}
```

Build and run:

```bash
export GENTEST_BAZEL_LOCAL_CLANG=/usr/lib/llvm-23/bin/clang++
bazel test --repo_env=GENTEST_BAZEL_LOCAL_CLANG //:my_tests
bazel run --repo_env=GENTEST_BAZEL_LOCAL_CLANG //:my_tests -- --list
```

On macOS, also name the SDK:

```bash
export GENTEST_BAZEL_LOCAL_CLANG=/opt/homebrew/opt/llvm/bin/clang++
export GENTEST_BAZEL_LOCAL_SDKROOT="$(xcrun --show-sdk-path)"
bazel test --repo_env=GENTEST_BAZEL_LOCAL_CLANG --repo_env=GENTEST_BAZEL_LOCAL_SDKROOT //:my_tests
```

`gentest_attach_codegen_textual` creates a `cc_test`, so `bazel test` runs all
of its cases in one process. Use `bazel run` to pass runner options such as
`--list` or `--run=demo/addition` (see [Command-line options](../command_line.md)).

The first build compiles `gentest_codegen` with CMake
(`@gentest//:gentest_codegen`) using one job, which takes a few minutes. That
step finds Clang and the LLVM/Clang CMake packages through `CC`, `CXX`,
`LLVM_DIR` and `Clang_DIR`, or in common install
locations: `/usr/lib/llvm-<N>`, `/usr/lib64/llvm<N>` and Homebrew's
`llvm@<N>`. If it stops with `gentest bazel: supported LLVM_DIR not found`,
set those variables and forward them to the tool build, for example with
`--host_action_env=LLVM_DIR --host_action_env=Clang_DIR`.

The fallback's codegen actions don't use remote execution or any action cache;
see [Local fallback](#local-fallback).

## Rules

| Rule | Creates |
| --- | --- |
| `gentest_attach_codegen_textual` | A `cc_test` from annotated headers and an optional `.cpp` file. |
| `gentest_add_mocks_textual` | A `cc_library` with gentest mocks generated from one mock definitions header. |
| `gentest_attach_codegen_modules` | A `cc_test` from a named-module interface unit. |
| `gentest_add_mocks_modules` | A `cc_library` with a generated mock module. |
| `gentest_codegen_toolchain` | The toolchain that supplies `gentest_codegen` and the host Clang. See [Exec toolchain contract](#exec-toolchain-contract). |
| `GentestGeneratedInfo` | Not a rule: the information a mock target's codegen step passes to the codegen of tests that use it (include roots, definitions, module names, inputs). You need it only to write your own rules. |
| `gentest_suite` | A shorthand for gentest's own repository layout: a `cc_test` named `gentest_<name>_bazel` from `tests/<name>/cases.cpp` (and `tests/<name>/cases.hpp` when it exists), linked with `gentest_main`. Use `gentest_attach_codegen_textual` in your project. |

Create mock targets first, then list them in a test target's `mock_targets`.
The test and mock rules also accept `codegen_host_clang`, but only to stop
analysis when it is not empty: register an exec toolchain instead.

## Test targets

`gentest_attach_codegen_textual` needs `src`, `source_hdrs`, or both:

- **Header-only.** List the annotated headers in `source_hdrs` and leave out
  `src`. Codegen scans each header on its own.
- **Out-of-line definitions.** Also pass one `src` file, which must include the
  annotated headers listed in `source_hdrs`. It is the only file codegen scans
  (see [Writing tests](../writing_tests.md)).

| Attribute | Meaning |
| --- | --- |
| `name` | The `cc_test` name. |
| `source_hdrs` | Annotated headers and other headers that the test reads, as file paths in the same package. |
| `src` | One `.cpp` file with out-of-line definitions. |
| `main` | A source file with your own `main()`. Without it, the test links `@gentest//:gentest_main`. The test always links `@gentest//:gentest_runtime`. |
| `mock_targets` | Mock targets from `gentest_add_mocks_textual` in the same package. |
| `deps` | Libraries the test uses. Their headers, include roots and definitions (`CcInfo`) reach codegen too. |
| `defines` | Definitions for codegen and the compiler. |
| `clang_args` | Flags for codegen and the compiler. |
| `source_includes` | Include directories relative to the target package, shared by codegen and the compiler. Declare the headers in `source_hdrs` or `deps`. |
| `linkopts`, `visibility` | Passed to the `cc_test`. |

Codegen runs as a Bazel action and reads only declared files. Declare every
header it reads through `source_hdrs` (files in this package), `deps` (headers
from other packages), a mock target, or gentest's own headers. A label from
another package in `source_hdrs` stops loading with:

```text
source_hdrs accepts same-package file paths only; move '<label>' to deps so its CcInfo preserves cross-package include semantics
```

For example, a test whose definitions live in a `.cpp` file and that uses a
library from another package:

```starlark
gentest_attach_codegen_textual(
    name = "service_tests",
    src = "tests/service_tests.cpp",
    source_hdrs = ["tests/service_tests.hpp"],
    deps = ["//app:service"],
)
```

### Death tests

Declare one Bazel target per [death test](../death_tests.md). The suite can
use either header or module registration.

```starlark
load("@gentest//bazel:defs.bzl", "gentest_death_test")

gentest_death_test(
    name = "fatal_input",
    suite = ":service_tests",
    case = "crash/fatal_input",
    expect = "invalid input",
)
```

`bazel test //:fatal_input` launches `gentest_supervise` with the suite from
the target's runfiles. The test uses Bazel's Bash toolchain, including on
Windows. Set `timeout_ms` for a total deadline,
`inactivity_ms` for a progress deadline, or `extra_args` for the test executable.
Bazel counts a runtime skip as a successful test. Its test log records
`[ SKIP ]` with the case name.

## Mocks

`gentest_add_mocks_textual` creates a library of gentest mocks from a mock
definitions header. A test uses it by listing it in `mock_targets`. The rules
generate gentest mocks only; the gMock and Trompeloeil backends need CMake. See
[Mocks](../mock_generation.md) for writing mock definitions and expectations.

```starlark
load("@gentest//bazel:defs.bzl", "gentest_add_mocks_textual", "gentest_attach_codegen_textual")

gentest_add_mocks_textual(
    name = "app_mocks",
    defs = ["tests/mock_defs.hpp"],
    public_header = "app_mocks.hpp",
)

gentest_attach_codegen_textual(
    name = "my_tests",
    source_hdrs = ["tests/cases.hpp"],
    mock_targets = [":app_mocks"],
)
```

Write the mock definitions header and the test as in
[Mocks](../mock_generation.md). The test includes the generated header by its
`public_header` name, for example `#include "app_mocks.hpp"`.

| Attribute | Meaning |
| --- | --- |
| `name` | The `cc_library` name; list it in `mock_targets`. |
| `defs` | Exactly one mock definitions header, as a file path in the same package. |
| `public_header` | File name of the generated header that tests include. |
| `deps` | Libraries whose headers the mock definitions header includes. Their `CcInfo` also reaches the codegen of tests that use the mocks. |
| `defines`, `clang_args` | Used for the mock codegen and the mock library only; tests that use the mocks don't get them. |
| `linkopts`, `visibility` | Passed to the `cc_library`. |

When `defs` is in a subdirectory of the package, every header below that
subdirectory (`.h`, `.hh`, `.hpp`, `.hxx`, `.inc`) is declared as a codegen
input automatically. Other headers must come through `deps`.

## Named modules

Module rules need:

- `--experimental_cpp_modules`, for example in your `.bazelrc`;
- Clang as the C++ compiler of your tests. The rules turn on Bazel's
  `cpp_modules` feature and compile with the Clang flag
  `-fmodules-embed-all-files`. Bazel's auto-configured C++ toolchain uses the
  compiler named by `CC`, so point `CC` and `CXX` at Clang and forward them
  with `--repo_env`;
- `clang_scan_deps` in the exec toolchain. The local fallback finds
  `clang-scan-deps` next to `GENTEST_BAZEL_LOCAL_CLANG` or on `PATH`.

`.bazelrc`:

```text
build --experimental_cpp_modules
```

`BUILD.bazel`:

```starlark
load("@gentest//bazel:defs.bzl", "gentest_add_mocks_modules", "gentest_attach_codegen_modules")

gentest_add_mocks_modules(
    name = "app_module_mocks",
    defs = [
        "tests/service.cppm",
        "tests/mock_defs.cppm",
    ],
    defs_modules = [
        "app.service",
        "app.mock_defs",
    ],
    module_name = "app.mocks",
)

gentest_attach_codegen_modules(
    name = "my_module_tests",
    src = "tests/cases.cppm",
    main = "tests/main.cpp",
    mock_targets = [":app_module_mocks"],
    deps = ["@gentest//:gentest"],
)
```

In this example `tests/service.cppm` exports `app.service`,
`tests/mock_defs.cppm` exports `app.mock_defs` (it declares the mock alias and
re-exports `gentest.mock` and `app.service`), and `tests/cases.cppm` exports
`app.cases` and imports `gentest` and `app.mocks`. `tests/main.cpp` imports
`gentest` and `app.cases` and calls `gentest::run_all_tests`. Cases and
fixtures in a module must be exported; see [Named modules](../modules.md). The
tested example has complete files of this shape, with other module names, in
[`tests/downstream/bazel_bzlmod_consumer/tests`](../../tests/downstream/bazel_bzlmod_consumer/tests).

Build and run:

```bash
export CC=/usr/lib/llvm-23/bin/clang
export CXX=/usr/lib/llvm-23/bin/clang++
export GENTEST_BAZEL_LOCAL_CLANG=/usr/lib/llvm-23/bin/clang++
bazel test --repo_env=CC --repo_env=CXX --repo_env=GENTEST_BAZEL_LOCAL_CLANG //:my_module_tests
bazel run --repo_env=CC --repo_env=CXX --repo_env=GENTEST_BAZEL_LOCAL_CLANG //:my_module_tests -- --list
```

`gentest_attach_codegen_modules` takes the attributes of
`gentest_attach_codegen_textual`, except:

- `src` is required: the module interface unit. The test compiles it
  unchanged as its `module_interfaces`.
- `main` is required.
- The test gets no gentest library automatically. List `@gentest//:gentest` in
  `deps`, and `@gentest//:gentest_bench_util` when you import
  `gentest.bench_util`.
- `mock_targets` come from `gentest_add_mocks_modules`.

`gentest_add_mocks_modules` takes these attributes. The library it creates
depends on `@gentest//:gentest` and `@gentest//:gentest_mock`.

| Attribute | Meaning |
| --- | --- |
| `name` | The `cc_library` name; list it in `mock_targets`. |
| `defs` | One or more module interface units. |
| `defs_modules` | The module name of each `defs` file, in the same order. |
| `module_name` | The name of the generated module that tests import. |
| `deps`, `defines`, `clang_args`, `linkopts`, `visibility` | As for `gentest_add_mocks_textual`. |

Codegen knows the module names of gentest's own modules and of the mock targets
in `mock_targets`. It doesn't map modules from other `deps`.

## Exec toolchain contract

Codegen actions take `gentest_codegen` and the host Clang only from an
exec-platform `gentest_codegen_toolchain`. They never take a compiler from the
target C++ toolchain, from `PATH` or from the action environment. That keeps a
host-built code generator out of target dependencies when you cross-compile;
[Host toolchains and cross-compiling](host_toolchain_sysroots.md) covers the
target side.

Define the toolchain in a tool package and register it from your root
`MODULE.bazel`. The labels below stand for repositories that you provide:

```starlark
# tools/gentest_codegen/BUILD.bazel
load("@gentest//bazel:defs.bzl", "gentest_codegen_toolchain")

gentest_codegen_toolchain(
    name = "impl",
    exec_os = "linux",
    codegen = "@my_gentest_tools//:gentest_codegen",
    clang = "@my_llvm//:clangxx",
    # Required by gentest_add_mocks_modules and gentest_attach_codegen_modules.
    clang_scan_deps = "@my_llvm//:clang_scan_deps",
    # Files that the executables' own files and runfiles don't already provide.
    runtime_files = [
        "@my_llvm//:clang_runtime_files",
        "@my_llvm//:cxx_standard_library_files",
    ],
    # Ordered marker files, each directly inside a C++ standard library include root.
    cxx_standard_library_roots = [
        "@my_llvm//:libcxx/include/c++/v1/gentest_root.marker",
    ],
    # Linux and Windows: ordered marker files for Clang's resource headers and
    # every C system include root.
    system_include_roots = [
        "@my_llvm//:lib/clang/23/include/gentest_root.marker",
        "@my_llvm//:sysroot/usr/include/gentest_root.marker",
    ],
)

toolchain(
    name = "registered",
    toolchain = ":impl",
    toolchain_type = "@gentest//bazel:gentest_codegen_toolchain_type",
    exec_compatible_with = ["@platforms//os:linux"],
)
```

```starlark
# MODULE.bazel
bazel_dep(name = "platforms", version = "1.0.0")
register_toolchains("//tools/gentest_codegen:registered")
```

| Attribute | Required | Meaning |
| --- | --- | --- |
| `codegen` | yes | Executable label of `gentest_codegen` for the exec platform. |
| `clang` | yes | Executable label of the host Clang. |
| `clang_scan_deps` | for module rules | Executable label of `clang-scan-deps`. |
| `exec_os` | yes | `linux`, `macos` or `windows`: how gentest invokes the tools. |
| `runtime_files` | no | Every other file the tools need: Clang's resource directory, shared libraries (DLLs on Windows), the `clang-scan-deps` closure, the complete C++ standard library header trees and, on macOS, the SDK. |
| `cxx_standard_library_roots` | yes | Ordered marker files, each directly inside a C++ standard library include root. |
| `system_include_roots` | Linux and Windows | Ordered marker files for Clang's resource include directory and every C or SDK system include root. On Windows these are the Clang resource, MSVC, UCRT and Windows SDK include roots. |
| `macos_sdk_root` | macOS | A marker file directly under the SDK root, or the SDK tree itself. |

The attributes `local_only`, `local_clang_path` and `local_macos_sdk_root` are
for gentest's local fallback; don't set them in a packaged toolchain. Analysis
stops with a message that names the attribute when a required one is missing.

How codegen actions use the toolchain:

- Each action gets `codegen` and `clang` with their runfiles, `runtime_files`
  and the marker files as inputs. Only named-module actions get
  `clang_scan_deps`.
- On Linux and Windows, gentest passes `-nostdinc` and adds back only the
  declared C++ and system include roots, in order, so no installed system
  headers outside them reach the action.
- On macOS, gentest passes `-nostdinc++`, adds back the declared C++ roots, and
  sets `SDKROOT` to the declared SDK path.
- Apart from `SDKROOT`, the action environment is empty, so `PATH` and
  variables such as `GENTEST_CODEGEN_HOST_CLANG` have no effect in Bazel
  builds.

`exec_os` doesn't constrain Bazel's toolchain resolution. Give every
`toolchain(...)` the matching `exec_compatible_with`: `@platforms//os:linux`,
`@platforms//os:osx` or `@platforms//os:windows`. Otherwise Bazel can select a
packaged toolchain for the wrong execution platform before gentest sees
`exec_os`.

A label that points at an absolute host path, or at a ccache wrapper, is not
portable to remote executors. On macOS, a copy of `/usr/bin/clang++` alone is
not a packaged toolchain: its resource directory belongs to the active Xcode
toolchain. Use the local fallback for that driver, or package a relocatable
LLVM distribution together with its resource and runtime files.

When no codegen toolchain matches, analysis stops with
`<target>: missing Gentest exec codegen toolchain. ...`, followed by what to
register.

## Local fallback

gentest's `MODULE.bazel` registers a fallback exec toolchain from the
repository `@gentest_local_exec_tools`. The repository rule reads these
variables; pass each one with `--repo_env`:

| Variable | Meaning |
| --- | --- |
| `GENTEST_BAZEL_LOCAL_CLANG` | Full path of the host `clang++`. Without it, the fallback is not available. |
| `GENTEST_BAZEL_LOCAL_CLANG_SCAN_DEPS` | The `clang-scan-deps` to use. Default: `clang-scan-deps-<N>` (for a Clang named `clang++-<N>` or `clang-<N>`) or `clang-scan-deps` next to the Clang, then the same names on `PATH`. |
| `GENTEST_BAZEL_LOCAL_SDKROOT` | macOS only, and required there: the SDK path, for example from `xcrun --show-sdk-path`. It must contain `SDKSettings.json` or `SDKSettings.plist`. |

The fallback:

- uses `@gentest//:gentest_codegen`, which builds `gentest_codegen` on the
  host with CMake (a Release build of the `gentest_codegen` target);
- runs codegen with the absolute path of your Clang and, on macOS, of your
  SDK;
- marks every codegen action `no-remote`, `no-cache` and `no-sandbox`, which
  disables remote execution and every action cache, including the local and
  disk caches;
- registers for Linux and macOS execution platforms only. On Windows, register
  a packaged toolchain whose `runtime_files` contain the LLVM and Clang DLLs
  and Clang's resource headers.

Use the fallback for local correctness checks. When you register a packaged
toolchain for cached or remote builds, leave `GENTEST_BAZEL_LOCAL_CLANG`
unset.

## Generated files and caching

Each rule writes its generated files to `gen/<name>/` in its package's output
directory, among them `<name>.artifact_manifest.json` for test rules (see
[Codegen artifact protocols](../codegen_artifact_protocols.md)). The generated
registration sources never include your `src`, and a module test compiles its
`src` unchanged. The rules also create helper targets named `<name>__codegen`
(with `<name>__codegen__srcs` and similar file groups) and
`<name>__source_hdr`; don't use those names for your own targets.

With a packaged toolchain, codegen actions have fixed arguments, declared
inputs and outputs, and an empty environment, so Bazel's local, disk and remote
caches reuse them; gentest keeps no parse cache of its own. A change to any
declared header reruns codegen. To check cache reuse, inspect the actions with
`bazel aquery`, then confirm with `--subcommands` or an execution log that a
second build doesn't run codegen.

## Limitations and tested example

- gentest's Bazel support builds from source; there is no prebuilt binary
  package.
- Named-module builds depend on your C++ toolchain's module support.
- Bazel's local macOS C++ toolchain doesn't declare an SDK settings file that
  Clang 23 and newer read, so gentest's macOS consumer test is skipped with
  those Clang versions.

[`tests/downstream/bazel_bzlmod_consumer`](../../tests/downstream/bazel_bzlmod_consumer)
is a complete consumer workspace with header-registration and module tests and
mocks. The CTest test `gentest_bazel_bzlmod_consumer` builds and runs it with
the local fallback; CI runs it on Ubuntu 24.04 and Fedora 43. On macOS, CI runs
only `gentest_bazel_codegen_action_cache`, which checks codegen command lines
and cache reuse with a packaged toolchain. CI doesn't build with Bazel on
Windows. To run the consumer test in a gentest build tree:

```bash
ctest --preset=debug-system --output-on-failure -R '^gentest_bazel_bzlmod_consumer$'
```
