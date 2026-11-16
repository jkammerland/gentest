# Host toolchains and cross-compiling

`gentest_codegen` runs on the machine that runs your build, and it uses a Clang
executable from that machine. This page shows how codegen finds that Clang, how
to choose a different one, and how to build your tests for another target or
against a sysroot.

## Two compilers

Your project's compiler builds the tests; it can be Clang, GCC, MSVC or a
cross compiler. `gentest_codegen` parses your sources with the Clang libraries
it was built with, and runs a Clang executable from the build machine, the host
Clang, to find Clang's built-in headers and to precompile named-module
interfaces. For named modules it also runs `clang-scan-deps`.

In a native build you usually set nothing: if your compiler is Clang, codegen
uses it, and otherwise it looks for a Clang on `PATH`. A cross build also needs
a `gentest_codegen` built for the build machine, because one built for the
target can't run there.

## Cross-compile a CMake project

This example builds tests for 64-bit Arm Linux on an x86-64 Linux machine. It
uses the GNU cross compiler (`aarch64-linux-gnu-g++`, for example from Ubuntu's
`g++-aarch64-linux-gnu` package) and runs the tests under QEMU (Ubuntu's
`qemu-user` package). It takes three builds: `gentest_codegen` for the build
machine, the gentest runtime for the target, and your project. Run the commands
from your project directory, with a gentest source checkout in its `gentest`
subdirectory.

### Build gentest_codegen for the build machine

Build only the code generator, with the `host-codegen` preset of the gentest
checkout:

```bash
cd gentest
cmake --preset=host-codegen
cmake --build --preset=host-codegen
cd ..
```

This writes `gentest/build/host-codegen/tools/gentest_codegen`. The build needs
Ninja and the LLVM/Clang development packages, version 19 or newer. If CMake
can't find them, add `-DLLVM_DIR=...` and `-DClang_DIR=...` to the first
`cmake` command (see [Install gentest on Linux](../install/linux.md)). The
`bin/gentest_codegen` of a native gentest install works as well. Either way,
use the same gentest version as the runtime you build next, because generated
code relies on gentest's internal headers.

### Build gentest for the target

Write a toolchain file for the target:

```cmake
# toolchains/aarch64-linux-gnu.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
# GCC ignores these. gentest passes them to codegen as --target.
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)

# Look for target libraries and packages only under the target roots.
list(APPEND CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Lets the build and CTest run aarch64 executables on this machine.
set(CMAKE_CROSSCOMPILING_EMULATOR qemu-aarch64 -L /usr/aarch64-linux-gnu)
```

Build the gentest runtime with it and install it into a prefix for the target:

```bash
cmake -S gentest -B gentest-aarch64-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/toolchains/aarch64-linux-gnu.cmake" \
  -Dgentest_INSTALL=ON \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/gentest-aarch64"
cmake --build gentest-aarch64-build
cmake --install gentest-aarch64-build
```

A cross build doesn't build `gentest_codegen` (`GENTEST_BUILD_CODEGEN` defaults
to `OFF`), and configure prints
`gentest: installing runtime without codegen; consumers must select a host tool`.
If CMake finds no fmt package for the target, the gentest build fetches fmt and
installs it into the same prefix.

### Build and run your tests

Your project's files are the same as for a native build. `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.31)
project(my_tests LANGUAGES CXX)

include(CTest)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(gentest CONFIG REQUIRED)

add_executable(my_tests cases.hpp)
target_compile_features(my_tests PRIVATE cxx_std_20)
target_link_libraries(my_tests PRIVATE gentest::gentest_main)

gentest_attach_codegen(my_tests)
gentest_discover_tests(my_tests)
```

`cases.hpp`:

```cpp
#pragma once

#include "gentest/test.h"

using namespace gentest::asserts;

[[using gentest: test("demo/addition")]]
inline void addition() {
    EXPECT_EQ(2 + 2, 4);
}
```

Configure with the same toolchain file. Add the target prefix to
`CMAKE_FIND_ROOT_PATH` so `find_package()` finds gentest and fmt there, and
pass the host code generator:

```bash
cmake -S . -B build-aarch64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/toolchains/aarch64-linux-gnu.cmake" \
  -DCMAKE_FIND_ROOT_PATH="$HOME/.local/gentest-aarch64" \
  -DGENTEST_CODEGEN_EXECUTABLE="$PWD/gentest/build/host-codegen/tools/gentest_codegen" \
  -DGENTEST_CODEGEN_HOST_CLANG=/usr/lib/llvm-23/bin/clang++
cmake --build build-aarch64
ctest --test-dir build-aarch64 --output-on-failure
```

The compile commands name a GCC, so codegen would look for a Clang itself;
`GENTEST_CODEGEN_HOST_CLANG` names the Clang of the LLVM that you built
`gentest_codegen` with. Without
`GENTEST_CODEGEN_EXECUTABLE` or `GENTEST_CODEGEN_TARGET`, a cross build stops
at configure with `cross-compiling requires a host gentest_codegen`.

`gentest_discover_tests()` and CTest run the test executable through
`CMAKE_CROSSCOMPILING_EMULATOR`. With the default `POST_BUILD` discovery, the
build itself runs it once to list the cases; `DISCOVERY_MODE PRE_TEST` moves
that step to `ctest` (see [CTest integration](../discover_tests.md)).

### What CMake passes to codegen

`gentest_attach_codegen()` gives codegen:

- each source's compile command from `compile_commands.json`, run with the
  host Clang when the command names another compiler;
- in a cross build, `--target=` from `CMAKE_CXX_COMPILER_TARGET` (or
  `CMAKE_C_COMPILER_TARGET`), `--sysroot=` from `CMAKE_SYSROOT`, and
  `--gcc-toolchain=` set to the parent of the compiler's directory (`/usr` for
  `/usr/bin/aarch64-linux-gnu-g++`);
- `-isystem` for each of the compiler's implicit include directories, so codegen
  sees the target's standard library headers;
- `GENTEST_CODEGEN_DEFAULT_CLANG_ARGS`, then the `CLANG_ARGS` of
  `gentest_attach_codegen()`. The default arguments are
  `-Wno-unknown-attributes;-Wno-attributes;-Wno-unknown-warning-option`; set
  the variable empty to drop them.

Don't repeat `--target` or `--sysroot` in `CLANG_ARGS`; because `CLANG_ARGS`
come last, you can use them to override one. With a Clang cross compiler, the
compile commands name that Clang and codegen uses it, so you rarely need
`GENTEST_CODEGEN_HOST_CLANG`.

### Use an imported target for the code generator

Instead of a path, you can name an imported executable target in
`GENTEST_CODEGEN_TARGET`. Set it before `gentest_attach_codegen()`:

```cmake
add_executable(host_gentest_codegen IMPORTED)
set_target_properties(host_gentest_codegen PROPERTIES
    IMPORTED_LOCATION /opt/gentest-host/bin/gentest_codegen)
set(GENTEST_CODEGEN_TARGET host_gentest_codegen)
```

A cross build rejects a target that isn't imported, because a target built in
the cross build can't run on the build machine.

## How codegen finds the host Clang

When `gentest_codegen` needs a Clang executable, it takes the first of:

1. `--host-clang <path>`, which the build system integrations pass from their
   host Clang setting (see [Set the host Clang](#set-the-host-clang)).
2. The `GENTEST_CODEGEN_HOST_CLANG` environment variable of the build (not of
   configure).
3. The compiler in the source's compile command, if it is a Clang. A bare
   `clang`, `clang++` or `clang-cl` without a path goes through steps 4 and 5
   first, and is used as written only if they find no Clang.
4. `CXX`, then `CC`, from the environment, if either names a Clang.
5. The first of `clang++-<N>`, `clang++`, `clang-<N>` and `clang` on `PATH`,
   where `<N>` is the LLVM major version that `gentest_codegen` was built with.
   On Windows the order is `clang++.exe`, `clang++`, `clang.exe`, `clang`, then
   the same names with `-<N>`.
6. Otherwise, `clang++`.

A value in steps 1 and 2 must name the Clang driver itself: a file named
`clang`, `clang++`, `clang-cl`, `clang-<N>` or `clang++-<N>`, with or without
`.exe`, as a full path or a name on `PATH`. For any other value, such as `c++`
or a compiler launcher, codegen prints for example
`gentest_codegen: warning: ignoring GENTEST_CODEGEN_HOST_CLANG='<value>' because it is not clang-like`
and continues with step 3. Use a Clang from the LLVM that `gentest_codegen` was
built with.

## Set the host Clang

| Build system | Setting |
| --- | --- |
| CMake | `-DGENTEST_CODEGEN_HOST_CLANG=<path>`. gentest passes it to every codegen command as `--host-clang`. |
| Meson | The `codegen_host_clang` option of the gentest subproject, as an absolute path. See [Meson](meson.md). |
| Xmake | `codegen = { clang = ... }` in `gentest_configure()`, or the `GENTEST_CODEGEN_HOST_CLANG` environment variable. See [Xmake](xmake.md). |
| Bazel | The `clang` label of your `gentest_codegen_toolchain`, or `GENTEST_BAZEL_LOCAL_CLANG` for the local fallback; Bazel ignores `GENTEST_CODEGEN_HOST_CLANG`. See [Bazel](bazel.md#exec-toolchain-contract). |

Outside Bazel the setting is optional. Set it when the lookup would pick the
wrong Clang: your compiler isn't Clang, several LLVM versions are installed, or
you want codegen to use another Clang than the one that compiles your tests.

## Find clang-scan-deps

Module registration and named-module mock targets also run `clang-scan-deps`.
`gentest_codegen` uses
`--clang-scan-deps <path>` or the `GENTEST_CODEGEN_CLANG_SCAN_DEPS` environment
variable. Without either, it tries `clang-scan-deps-<N>` when the host Clang's
name ends in `-<N>`, then `clang-scan-deps`, each first next to the host Clang
and then on `PATH`.

CMake passes `--clang-scan-deps` for named-module targets. It uses the first
of:

1. `GENTEST_CODEGEN_CLANG_SCAN_DEPS`, as a CMake variable or else as an
   environment variable. A path you set is used as given, even if it doesn't
   exist.
2. `CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS`, the scanner CMake uses for C++
   modules.
3. Next to `GENTEST_CODEGEN_HOST_CLANG`, then next to `CMAKE_CXX_COMPILER`:
   `clang-scan-deps-<N>` when that executable's name ends in a version number
   `<N>`, then `clang-scan-deps`.
4. `clang-scan-deps` in `LLVM_TOOLS_BINARY_DIR`, when LLVM's CMake package is
   loaded.
5. On `PATH`: `clang-scan-deps-<N>` for the loaded LLVM version, then
   `clang-scan-deps-23` down to `clang-scan-deps-19`, then `clang-scan-deps`.

When it finds none, configure stops with
`named-module codegen requires clang-scan-deps`.

In Xmake, set `codegen = { scan_deps = ... }` or
`GENTEST_CODEGEN_CLANG_SCAN_DEPS`; otherwise the helper looks next to the host
Clang. In Bazel, set the `clang_scan_deps` label of the toolchain.
[Named modules](../modules.md#requirements) lists the other module
requirements.

## Cross-compile with Meson, Xmake or Bazel

CI tests cross builds only with CMake (see
[Cross-testing gentest under QEMU](../cross_compile/linux_qemu.md)). The other
build systems follow the same rules: a `gentest_codegen` built for the build
machine, a gentest runtime built for the target, and target flags that you pass
to codegen yourself.

### Meson

Your cross file picks the compiler for your tests and gentest's runtime, but
codegen doesn't see it. Repeat the target flags in the `clang_args` of each
`gentest_textual_suites` and `gentest_textual_mocks` entry, and point
`codegen_path` at a `gentest_codegen` built for the build machine:

```meson
gentest_sp = subproject(
  'gentest',
  default_options: [
    'build_self_tests=false',
    'codegen_path=/opt/gentest-host/bin/gentest_codegen',
    'codegen_host_clang=/usr/lib/llvm-23/bin/clang++',
  ],
)

gentest_textual_suites = [
  {
    'name': 'my_tests',
    'declaration_headers': ['tests/cases.hpp'],
    'source_includes': ['tests'],
    'clang_args': ['--target=aarch64-linux-gnu', '--sysroot=/opt/sysroots/aarch64-linux-gnu'],
  },
]

subdir('subprojects/gentest/meson/textual')
```

### Xmake

Codegen gets `gentest_common_cxxflags` and each call's `clang_args`. Both also
go to the compiler, so put the target flags there. Point `gentest_root` at a
gentest install built for the target and `codegen.exe` at a `gentest_codegen`
built for the build machine:

```lua
gentest_configure({
    project_root = os.projectdir(),
    gentest_root = "/opt/gentest-aarch64",
    incdirs = {"tests"},
    gentest_common_defines = {},
    gentest_common_cxxflags = {
        "--target=aarch64-linux-gnu",
        "--sysroot=/opt/sysroots/aarch64-linux-gnu",
    },
    codegen = {
        exe = "/opt/gentest-host/bin/gentest_codegen",
        clang = "/usr/lib/llvm-23/bin/clang++",
    },
})
```

Select the target compiler with `xmake f` as usual. [Xmake](xmake.md) covers
the remaining keys, named modules and the macOS `--sdk` setting.

### Bazel

Bazel takes codegen's tools from the exec-platform `gentest_codegen_toolchain`,
never from the target C++ toolchain, so a cross build runs codegen on the build
machine without extra settings (see the
[exec toolchain contract](bazel.md#exec-toolchain-contract)). Codegen actions
don't see your C++ toolchain's flags; pass target-specific flags or macros with
`clang_args` or `defines`. Both also go to the compiler, so use flags that your
target compiler accepts.

## Sysroots without cross-compiling

A native CMake build that sets `CMAKE_SYSROOT` needs nothing else: the compile
commands carry `--sysroot`, and codegen reads them. With Meson, Xmake or Bazel,
pass `--sysroot` to codegen as in the cross examples above.

On macOS, when neither the compile command nor the extra Clang arguments name
a sysroot (`-isysroot` or `--sysroot`), codegen adds `-isysroot` with the SDK
from the `SDKROOT` environment variable, or else from
`xcrun --sdk macosx --show-sdk-path`. Bazel's codegen actions get `SDKROOT`
from the toolchain's `macos_sdk_root`.

## Troubleshooting

To see which Clang and flags codegen uses, set
`GENTEST_CODEGEN_LOG_PARSE_COMMANDS=1` in the build's environment, for example
`GENTEST_CODEGEN_LOG_PARSE_COMMANDS=1 cmake --build build`. Codegen then prints
each parse command, starting with the Clang it chose and its `-resource-dir=`.

| Message | Fix |
| --- | --- |
| `cross-compiling requires a host gentest_codegen` | Set `GENTEST_CODEGEN_EXECUTABLE` or `GENTEST_CODEGEN_TARGET`. |
| `GENTEST_CODEGEN_EXECUTABLE='<path>' does not exist or is not a file` | Point it at the `gentest_codegen` executable itself. |
| `named-module codegen requires clang-scan-deps` | Install `clang-scan-deps` with the host LLVM, or set `GENTEST_CODEGEN_CLANG_SCAN_DEPS`. |
| `gentest_codegen: warning: ignoring ... because it is not clang-like` | Name the Clang executable itself, not a wrapper or a `c++` alias. |
| Parse errors in standard headers in a cross build | Check that `--target` and the sysroot reach codegen: set `CMAKE_CXX_COMPILER_TARGET` with a GCC cross compiler, or repeat the flags in `clang_args` with the other build systems. |
