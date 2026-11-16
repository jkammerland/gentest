# Install gentest on macOS

This page takes you from source to a passing test in your own CMake project on
macOS. You build gentest with Homebrew LLVM, install it into a folder you own,
and run a first test. [Install gentest on Linux](linux.md) explains each step
in more detail.

## Requirements

| Component | Requirement |
| --- | --- |
| macOS SDK | The Xcode Command Line Tools. |
| Build tools | CMake 3.31 or newer, Ninja, and Git. |
| Code generator | Homebrew LLVM, version 19 or newer. `gentest_codegen` links its LLVM and Clang libraries, which Xcode doesn't provide, so you need it even if you compile with AppleClang. |
| Compiler for your tests | Homebrew LLVM's `clang++`, or AppleClang 17 (Xcode 26) or newer. |

Older AppleClang releases fail gentest's configure check for
`std::stop_token`, so use Xcode 26 or newer, or Homebrew LLVM. Named modules
(`import gentest;`) need Homebrew LLVM and Ninja 1.11 or newer; see
[Use named modules](#use-named-modules).

Install the tools:

```bash
xcode-select --install
brew install cmake ninja llvm@23
```

The commands on this page use `llvm@23`. Homebrew installs it keg-only (not
on `PATH`), so the commands name its compiler and CMake packages explicitly.
Set these variables in every shell you build in:

```bash
llvm_prefix="$(brew --prefix llvm@23)"
export SDKROOT="$(xcrun --show-sdk-path)"
```

`SDKROOT` tells Homebrew's `clang++` and `gentest_codegen` which macOS SDK to
use. Check the tools:

```bash
"$llvm_prefix/bin/clang++" --version
cmake --version
ninja --version
```

## Build and install gentest

Run these commands from the folder where you keep source checkouts:

```bash
git clone https://github.com/jkammerland/gentest.git
gentest_source="$PWD/gentest"
gentest_prefix="$HOME/.local/gentest"

cmake -S "$gentest_source" -B "$gentest_source/build/install" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$llvm_prefix/bin/clang" \
  -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" \
  -DLLVM_DIR="$llvm_prefix/lib/cmake/llvm" \
  -DClang_DIR="$llvm_prefix/lib/cmake/clang" \
  -Dgentest_INSTALL=ON \
  -DCMAKE_INSTALL_PREFIX="$gentest_prefix"
cmake --build "$gentest_source/build/install"
cmake --install "$gentest_source/build/install"
"$gentest_prefix/bin/gentest_codegen" --help
```

The last command prints `OVERVIEW: gentest clang code generator`. A test
executable's `--help` prints the installed gentest version on its first line
(`gentest v<version>`). If CMake
found an installed fmt, your project must find the same fmt version;
otherwise the build downloads fmt and installs it into the gentest prefix.

## Write your first test project

Create a folder next to the gentest checkout with two files.
[Write your first test project](linux.md#write-your-first-test-project)
explains what each part does.

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.31)
project(first_test LANGUAGES CXX)

include(CTest)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(gentest CONFIG REQUIRED)

add_executable(first_tests cases.hpp)
target_compile_features(first_tests PRIVATE cxx_std_20)
target_link_libraries(first_tests PRIVATE gentest::gentest_main)

gentest_attach_codegen(first_tests)
gentest_discover_tests(first_tests)
```

`cases.hpp`:

```cpp
#pragma once

#include "gentest/test.h"

[[gentest::test("demo/addition")]]
inline void addition() {
    const auto value = 2 + 2;
    gentest::expect_eq(value, 4, "addition result");
}
```

From that folder, build with the same compiler you used for gentest, and run
the tests:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" \
  -DCMAKE_PREFIX_PATH="$gentest_prefix"
cmake --build build
ctest --test-dir build --output-on-failure
```

CTest runs one passing test named `demo/addition`. To see a failure, change
the expected value in `cases.hpp` to `5`, rebuild, and run only that case:

```bash
cmake --build build
./build/first_tests --run=demo/addition
```

The output shows the failed assertion and a `Rerun:` command for this case;
[See a failure and rerun one case](linux.md#see-a-failure-and-rerun-one-case)
shows it in full.

## Use AppleClang

AppleClang 17 or newer can build gentest and your tests. Pass
`/usr/bin/clang` and `/usr/bin/clang++` as `CMAKE_C_COMPILER` and
`CMAKE_CXX_COMPILER` in both configure commands. Keep `LLVM_DIR` and
`Clang_DIR` pointing at Homebrew LLVM, because `gentest_codegen` links its
libraries with either compiler.

In your project, also pass
`-DGENTEST_CODEGEN_HOST_CLANG="$llvm_prefix/bin/clang++"`. `gentest_codegen`
then reads Clang's builtin headers from the LLVM release it was built with,
instead of from AppleClang. [Host toolchains and cross-compiling](../buildsystems/host_toolchain_sysroots.md)
explains how codegen picks its host Clang.

CMake doesn't support C++ module scanning with AppleClang, so named modules
need Homebrew LLVM.

## Use named modules

Public named modules (`import gentest;`) are off by default. Turn them on with
Homebrew LLVM and Ninja 1.11 or newer by configuring the gentest build again,
then build and install:

```bash
cmake -S "$gentest_source" -B "$gentest_source/build/install" \
  -DGENTEST_ENABLE_PUBLIC_MODULES=ON \
  -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="$llvm_prefix/bin/clang-scan-deps"
cmake --build "$gentest_source/build/install"
cmake --install "$gentest_source/build/install"
```

CMake's module support uses `clang-scan-deps`, which Homebrew LLVM includes.
With `GENTEST_ENABLE_PUBLIC_MODULES=ON`, configure stops if the toolchain
can't build the modules; with `AUTO`, it turns them off instead.

Build the module example to check the install:

```bash
cmake -S "$gentest_source/examples/hello_modules" -B build-hello-modules -G Ninja \
  -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" \
  -DCMAKE_PREFIX_PATH="$gentest_prefix"
cmake --build build-hello-modules
ctest --test-dir build-hello-modules --output-on-failure
```

[Named modules](../modules.md) covers `import gentest;`, cases in module
interface units (`MODULE_REGISTRATION`) and module mocks.

## Next steps

The rest of the Linux guide applies on macOS too:

- [Keep definitions in a source file](linux.md#keep-definitions-in-a-source-file)
- [Try the examples](linux.md#try-the-examples)
- [Install the runtime without the code generator](linux.md#install-the-runtime-without-the-code-generator)
- [Cross-compiling](linux.md#cross-compiling)

[Writing tests](../writing_tests.md) explains how to write cases, and
[Command-line options](../command_line.md) lists the runner options.
