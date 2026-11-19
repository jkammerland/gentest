# Xmake

This page shows how to use gentest in an Xmake project. A gentest install
contains a Lua helper, `share/gentest/xmake/gentest.lua`, whose functions add
`gentest_codegen` steps to your Xmake targets.

The helper supports header registration and named modules. It calls them
`kind = "textual"` (the default) and `kind = "modules"`: in gentest's Xmake
API, textual means header registration. To compare Xmake with the other
integrations, see [Choosing a build system](downstream_consumers.md).

## Quick start

You need:

- a gentest install made with CMake and `-Dgentest_INSTALL=ON` (see
  [Install gentest on Linux](../install/linux.md)). It contains `bin/gentest_codegen`,
  `include/gentest/` and `share/gentest/xmake/gentest.lua`;
- a host Clang for `gentest_codegen` to use. For header registration your test
  code can use any C++20 compiler.

The examples use `/opt/gentest` as the install prefix.

`xmake.lua`:

```lua
set_project("my_tests")
set_languages("cxx20")
add_rules("mode.debug", "mode.release")

local gentest_prefix = "/opt/gentest"

includes(path.join(gentest_prefix, "share", "gentest", "xmake", "gentest.lua"))

gentest_configure({
    project_root = os.projectdir(),
    gentest_root = gentest_prefix,
    incdirs = {"tests"},
    gentest_common_defines = {},
    gentest_common_cxxflags = {"-Wno-attributes"},
    codegen = {
        exe = path.join(gentest_prefix, "bin", "gentest_codegen"),
    },
})

target("my_tests")
    set_kind("binary")
    gentest_attach_codegen({
        name = "my_tests",
        headerfiles = {"tests/cases.hpp"},
        main = "tests/main.cpp",
        output_dir = "build/gentest/my_tests",
    })
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

`tests/main.cpp` (the helper links gentest's runtime but not its `main()`):

```cpp
#include "gentest/runner.h"

auto main(int argc, char **argv) -> int { return gentest::run_all_tests(argc, argv); }
```

Build and run:

```bash
xmake f -m debug
xmake build my_tests
xmake run my_tests
xmake run my_tests --list
```

Arguments after the target name go to the test executable (see
[Command-line options](../command_line.md)).

The helper links the runtime, notification and fmt libraries it finds in the
prefix's `lib` or `lib64` directory. A gentest build that fetched fmt itself
installs fmt there. If your prefix has no fmt library, add `add_requires("fmt")`
near the top of `xmake.lua`; the helper then adds the `fmt` package to each
target it configures.

On Linux, when the Gentest prefix supplies shared fmt, the helper adds its
library directory to the test executable's runtime search path. A target built
under the prefix uses an executable-relative path and can move with the SDK.
A target built elsewhere uses the configured prefix's library directory.

## Configuration

`gentest_configure({...})` sets the options that every later helper call uses.
Call it once, after `includes(...)` and before your targets; a second call
replaces every setting.

| Key | Required | Meaning |
| --- | --- | --- |
| `project_root` | yes | Usually `os.projectdir()`. Relative paths in helper calls are resolved against it. |
| `incdirs` | yes | Include directories for every helper target and for codegen. |
| `gentest_common_defines` | yes | Definitions for every helper target and for codegen; `-D` is added when missing. May be empty. |
| `gentest_common_cxxflags` | yes | C++ flags for every helper target and for codegen. |
| `gentest_root` | no | A gentest install prefix or source checkout: a directory with `include/gentest/runner.h` or `include/gentest/gentest.cppm`. Without it, the helper guesses from `helper_root`, then the project root. |
| `helper_root` | no | The directory that holds `gentest.lua`; used only to guess `gentest_root`. |
| `codegen` | no | `{ exe = ..., clang = ..., scan_deps = ... }`: see [Choosing the tools](#choosing-the-tools). |
| `gentest_module_files` | for modules | gentest's module interface files, absolute or relative to `gentest_root`. See [Named modules](#named-modules). |
| `codegen_project_root` | no | A gentest source checkout to build `gentest_codegen` from; see [Building gentest_codegen from a checkout](#building-gentest_codegen-from-a-checkout). |

A missing required key stops configuration, for example with
``gentest xmake: gentest_configure must provide `incdirs` ``. A `gentest_root`
without gentest's headers stops it with
``gentest xmake: gentest_configure `gentest_root` must point at a gentest source tree or installed prefix``.

## Choosing the tools

The helper resolves three tools when a target builds:

| Tool | Order |
| --- | --- |
| `gentest_codegen` | `codegen.exe`, then the `GENTEST_CODEGEN` environment variable, then a copy that the helper builds from a gentest checkout. |
| Host Clang | `codegen.clang`, then `GENTEST_CODEGEN_HOST_CLANG`, then the target's C++ compiler when it is Clang. Otherwise `gentest_codegen` looks for one itself; [Host toolchains and cross-compiling](host_toolchain_sysroots.md) lists that order. |
| `clang-scan-deps` (modules only) | `codegen.scan_deps`, then `GENTEST_CODEGEN_CLANG_SCAN_DEPS`, then the directory of the host Clang: `clang-scan-deps-<N>` next to `clang++-<N>`, then `clang-scan-deps`. |

A relative path is looked up under `project_root`; a bare name such as
`clang++` is looked up on `PATH`. A `codegen` entry, `GENTEST_CODEGEN_HOST_CLANG`
or `GENTEST_CODEGEN_CLANG_SCAN_DEPS` that doesn't resolve to a file stops the
build; a `GENTEST_CODEGEN` that doesn't resolve is skipped. Prefer the `codegen`
table when you own the `xmake.lua`, and the environment variables for
overrides from the shell. On Windows the code generator is
`gentest_codegen.exe`. To build for another target, see
[Host toolchains and cross-compiling](host_toolchain_sysroots.md).

## Test targets

`gentest_attach_codegen({...})` turns the current target into a gentest test
executable. Call it inside a `target(...)` block with `set_kind("binary")`.

For header registration (`kind = "textual"`), give `source`, `headerfiles`, or
both:

- **Header-only.** List the annotated headers in `headerfiles` and leave out
  `source`. Codegen scans each header on its own.
- **Out-of-line definitions.** Also pass one `source` file, which must include
  the annotated headers. It is the only file codegen scans (see
  [Writing tests](../writing_tests.md)).

| Option | Meaning |
| --- | --- |
| `name` | Required. The target name. |
| `kind` | `"textual"` (default) or `"modules"`. |
| `output_dir` | Required. Directory for the generated files, relative to `project_root` or absolute. Use one directory per target. |
| `headerfiles` | Annotated headers. |
| `source` | Header registration: one `.cpp` file with out-of-line definitions. Modules: the module interface unit, required. |
| `main` | An extra source file, normally your `main()`. |
| `includes` | Include directories for the target and for codegen. |
| `deps` | Targets to depend on: mock targets, a public-modules target, or `{ target = ..., include_dirs = {...} }` tables (also `include_dir`) whose directories reach codegen. |
| `defines` | Definitions for the target and for codegen. |
| `clang_args` | Flags for codegen; the helper also adds them to the target's C++ flags. |
| `module_name` | Modules: the name of the module that `source` exports. Required. |
| `public_modules_via_deps` | Modules: take gentest's modules from a `gentest_add_public_modules` target in `deps`. |

Codegen also sees `incdirs`, `gentest_common_defines`,
`gentest_common_cxxflags`, the include directories of the Xmake packages the
target uses, and the include directories of helper-created targets in `deps`.
It doesn't see the include directories or modules of your other Xmake targets;
pass those through `includes`, a `deps` table or `clang_args`.

### Death tests

To register a [death test](../death_tests.md), call `gentest_add_death_test`
after declaring the suite target. Use the full Gentest case name.

```lua
gentest_add_death_test({
    name = "fatal_input",
    suite = "my_tests",
    case = "crash/fatal_input",
    expect = "invalid input",
})
```

Run it with `xmake test fatal_input/death`. The helper uses the installed
`bin/gentest_supervise` or builds the supervisor alongside an in-tree suite.
It accepts optional `timeout_ms`, `inactivity_ms` and extra test executable
`args`. Xmake counts a runtime skip as a successful test and the supervisor
prints `[ SKIP ]` with the case name.

## Mocks

`gentest_add_mocks({...})` turns the current target into a mock target that
generates gentest mocks from mock definitions headers. A test target uses it
through `deps`. The helper generates gentest mocks only; the gMock and
Trompeloeil backends need CMake. See [Mocks](../mock_generation.md) for writing
mock definitions and expectations.

```lua
target("app_mocks")
    set_kind("static")
    gentest_add_mocks({
        name = "app_mocks",
        defs = {"tests/mock_defs.hpp"},
        header_name = "app_mocks.hpp",
        output_dir = "build/gentest/app_mocks",
    })

target("my_tests")
    set_kind("binary")
    gentest_attach_codegen({
        name = "my_tests",
        headerfiles = {"tests/cases.hpp"},
        main = "tests/main.cpp",
        output_dir = "build/gentest/my_tests",
        deps = {"app_mocks"},
    })
```

Write the mock definitions header and the test as in
[Mocks](../mock_generation.md). The test includes the generated header by its
`header_name`, for example `#include "app_mocks.hpp"`.

| Option | Meaning |
| --- | --- |
| `name` | Required. The target name. |
| `kind` | `"textual"` (default) or `"modules"`. |
| `defs` | Required. Mock definitions headers, or module interface units for modules. One or more. |
| `output_dir` | Required. Directory for the generated files. |
| `header_name` | Header registration: required. File name of the generated header that tests include. |
| `target_id` | Prefix of the generated file names. Default: `name` with every character other than a letter, digit or `_` replaced by `_`. |
| `headerfiles` | Headers to attach to the target. Default: `defs`. |
| `deps` | As for test targets. |
| `defines`, `clang_args` | As for test targets. |
| `defs_modules` | Modules: the module name of each `defs` file, in the same order. Required. |
| `module_name` | Modules: the name of the generated module that tests import. Required. |
| `public_modules_via_deps` | Modules: as for test targets. |

A header-registration mock target can take several mock definitions headers.

## Named modules

Module targets need more setup than header registration:

- Build the gentest install with `-DGENTEST_ENABLE_PUBLIC_MODULES=ON` as well.
  That installs `include/gentest/gentest.cppm`, `gentest.mock.cppm` and
  `gentest.bench_util.cppm`. See [Named modules](../modules.md#requirements)
  for compiler and Ninja requirements.
- Configure Xmake with Clang as the target compiler. Module calls stop with
  `... requires a Clang C++ target toolchain in Xmake` otherwise.
- Make `clang-scan-deps` resolvable (see [Choosing the tools](#choosing-the-tools)).
- List the module files in `gentest_module_files`.
- Create one `gentest_add_public_modules` target and set
  `public_modules_via_deps = true` on the other module targets. Only that
  target then compiles the `gentest`, `gentest.mock` and `gentest.bench_util`
  modules; without the flag, each module target compiles its own copy.

```lua
gentest_configure({
    project_root = os.projectdir(),
    gentest_root = gentest_prefix,
    incdirs = {"tests"},
    gentest_common_defines = {},
    gentest_common_cxxflags = {"-Wno-attributes"},
    gentest_module_files = {
        "include/gentest/gentest.cppm",
        "include/gentest/gentest.mock.cppm",
        "include/gentest/gentest.bench_util.cppm",
    },
    codegen = {
        exe = path.join(gentest_prefix, "bin", "gentest_codegen"),
        clang = "/usr/lib/llvm-23/bin/clang++",
        scan_deps = "/usr/lib/llvm-23/bin/clang-scan-deps",
    },
})

target("gentest_modules")
    set_kind("moduleonly")
    gentest_add_public_modules({
        output_dir = "build/gentest/modules",
    })

target("app_module_mocks")
    set_kind("static")
    add_deps("gentest_modules")
    gentest_add_mocks({
        name = "app_module_mocks",
        kind = "modules",
        defs = {"tests/service.cppm", "tests/mock_defs.cppm"},
        defs_modules = {"app.service", "app.mock_defs"},
        module_name = "app.mocks",
        output_dir = "build/gentest/app_module_mocks",
        public_modules_via_deps = true,
    })

target("my_module_tests")
    set_kind("binary")
    gentest_attach_codegen({
        name = "my_module_tests",
        kind = "modules",
        module_name = "app.cases",
        source = "tests/cases.cppm",
        main = "tests/main.cpp",
        output_dir = "build/gentest/my_module_tests",
        deps = {"gentest_modules", "app_module_mocks"},
        public_modules_via_deps = true,
    })
```

In this example `tests/service.cppm` exports `app.service`,
`tests/mock_defs.cppm` exports `app.mock_defs` (it declares the mock alias and
re-exports `gentest.mock` and `app.service`), and `tests/cases.cppm` exports
`app.cases` and imports `gentest` and `app.mocks`. Cases and fixtures in a
module must be exported; see [Named modules](../modules.md). The tested
example has complete files of this shape, with other module names, in
[`tests/downstream/xmake_xrepo_consumer/tests`](../../tests/downstream/xmake_xrepo_consumer/tests).

`gentest_add_public_modules({...})` takes `output_dir` (required), `defines`
and `clang_args`, and compiles gentest's module files as public module units of
its target.

Configure with Clang as the target compiler:

```bash
xmake f -m debug --cc=/usr/lib/llvm-23/bin/clang --cxx=/usr/lib/llvm-23/bin/clang++
xmake build my_module_tests
xmake run my_module_tests --list
```

### macOS

Pass `--toolchain=llvm`, and point `--sdk` at the LLVM prefix so that Xmake
doesn't fall back to Apple's compiler:

```bash
xmake f -m debug --toolchain=llvm --sdk=/opt/homebrew/opt/llvm@23 --cc=/opt/homebrew/opt/llvm@23/bin/clang --cxx=/opt/homebrew/opt/llvm@23/bin/clang++
```

### Windows

Use Xmake's LLVM toolchain. When you configure with `--toolchain=llvm` or a
Clang `--cxx` on Windows, each helper call also sets, on its target:

- the `llvm` toolchain;
- the static runtime: `MT`, or `MTd` in debug mode;
- the definitions `FMT_USE_CONSTEVAL=0`, `_ITERATOR_DEBUG_LEVEL=0` and
  `_HAS_ITERATOR_DEBUGGING=0`.

`GENTEST_XMAKE_WINDOWS_RUNTIME` overrides the runtime and
`GENTEST_XMAKE_WINDOWS_DEFINES` the definitions (separated by `;`). Setting
either one replaces both defaults, so set both. A gentest install built with
CMake and Clang on Windows uses the static release runtime `MT` in every
configuration. A debug-mode Xmake build against it needs `MT` too:

```powershell
$env:GENTEST_CODEGEN_HOST_CLANG = "C:/llvm/bin/clang++.exe"
$env:GENTEST_CODEGEN_CLANG_SCAN_DEPS = "C:/llvm/bin/clang-scan-deps.exe"
$env:GENTEST_XMAKE_WINDOWS_RUNTIME = "MT"
$env:GENTEST_XMAKE_WINDOWS_DEFINES = "FMT_USE_CONSTEVAL=0;_ITERATOR_DEBUG_LEVEL=0;_HAS_ITERATOR_DEBUGGING=0"
xmake f -c -y -m debug --toolchain=llvm --cc=C:/llvm/bin/clang.exe --cxx=C:/llvm/bin/clang++.exe
xmake build my_module_tests
```

Call `gentest_apply_windows_llvm_toolchain()` in other targets that should use
the same settings. It does nothing on other hosts.

## What the helper does

During the build, each test and mock target runs `gentest_codegen` before it
compiles anything, and targets that depend on a mock or public-modules target
wait for its generated files. Codegen reads `compile_commands.json` from the
Xmake build directory when it exists; otherwise the helper writes a small
compile database to `<output_dir>/.gentest_compdb/`. The generated
registration sources never include your `source`, and a module test target
compiles its authored `.cppm` unchanged. The artifact manifest
(`<target>.artifact_manifest.json`) is described in
[Codegen artifact protocols](../codegen_artifact_protocols.md).

### Building gentest_codegen from a checkout

Without `codegen.exe` and `GENTEST_CODEGEN`, the helper builds
`gentest_codegen` from a gentest source checkout. It runs a CMake Release build
of the `gentest_codegen` target in `<checkout>/build/xmake-codegen/<host>/<arch>/`
and reads that build's `compile_commands.json`. The checkout is
`codegen_project_root`, or the project root when it has a `CMakeLists.txt`.
Point your project at an installed `gentest_codegen` instead.

## Limitations and tested example

- gentest's install doesn't include an xrepo package definition. The tested
  example's local repository,
  [`repo/packages/g/gentest/xmake.lua`](../../tests/downstream/xmake_xrepo_consumer/repo/packages/g/gentest/xmake.lua),
  shows a package that wraps an install prefix.
- CI runs the Xmake tests on Linux only. The macOS and Windows settings above
  come from the consumer test script.

[`tests/downstream/xmake_xrepo_consumer`](../../tests/downstream/xmake_xrepo_consumer)
is a complete consumer project with header-registration and module targets and
mocks. The CTest test `gentest_xmake_xrepo_consumer` builds it against a fresh
install, and CI runs it on Ubuntu 24.04 and Fedora 43. To run that test in a
gentest build tree:

```bash
ctest --preset=debug-system --output-on-failure -R '^gentest_xmake_xrepo_consumer$'
```
