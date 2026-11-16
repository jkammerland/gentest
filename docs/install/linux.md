# Install gentest on Linux

This page takes you from source to a passing test in your own CMake project.
You build gentest and its code generator, install them into a folder you own,
write a first test, make it fail, and rerun just that case.

## Requirements

| Component | Requirement |
| --- | --- |
| Compiler for your tests | A C++20 compiler and standard library that provide `std::stop_token`. Clang and GCC both work. |
| Build tools | CMake 3.31 or newer, Ninja (or Ninja Multi-Config), and Git. |
| Code generator | LLVM and Clang development packages of one major version, 19 or newer, plus that version's `clang++` with its builtin headers. `gentest_codegen` links these libraries and runs on your build machine. |
| fmt | An installed fmt CMake package. Without one, the build downloads fmt 12.1.0. |

The commands below use one LLVM installation both to build your tests and as
the host Clang for `gentest_codegen`. If you build your tests with another
compiler, its C++ standard library must be ABI-compatible with the one that
built gentest. [Host toolchains and cross-compiling](../buildsystems/host_toolchain_sysroots.md)
covers separate compilers and cross builds. Named modules have extra
[requirements](../modules.md#requirements): a module-capable compiler, Ninja
1.11 or newer and `clang-scan-deps`.

### Install the packages

Install the requirements with your package manager. `<N>` is the LLVM major
version.

| Distribution | Packages |
| --- | --- |
| Fedora | `clang`, `clang-devel`, `llvm-devel`, `fmt-devel`, `cmake`, `ninja-build`, `git` |
| Debian, Ubuntu | `build-essential`, `clang-<N>`, `clang-tools-<N>`, `libclang-<N>-dev`, `libclang-cpp<N>-dev`, `llvm-<N>-dev`, `libfmt-dev`, `zlib1g-dev`, `libzstd-dev`, `libxml2-dev`, `libffi-dev`, `cmake`, `ninja-build`, `git` |
| Alpine | `build-base`, `clang<N>`, `clang<N>-dev`, `clang<N>-extra-tools`, `clang<N>-static`, `llvm<N>`, `llvm<N>-dev`, `llvm<N>-gtest`, `llvm<N>-static`, `fmt-dev`, `zlib-dev`, `zstd-dev`, `libxml2-dev`, `libffi-dev`, `linux-headers`, `cmake`, `ninja`, `git` |

Some releases ship an older CMake (Ubuntu 24.04 has 3.28); install a newer
one with, for example, `python3 -m pip install cmake`. On Debian and Ubuntu,
[apt.llvm.org](https://apt.llvm.org/) provides LLVM majors that your release
doesn't ship.

Debian, Ubuntu and Alpine install versioned commands such as `llvm-config-<N>`
and `clang++-<N>`. Put that LLVM's `bin` folder first on `PATH` before you run
the commands below, which also picks one of several installed versions:

```bash
export PATH="/usr/lib/llvm-<N>/bin:$PATH"   # Debian, Ubuntu
export PATH="/usr/lib/llvm<N>/bin:$PATH"    # Alpine
```

Check that `llvm-config` and `clang++` report the same major version:

```bash
llvm-config --version
clang++ --version
cmake --version
ninja --version
```

Don't mix LLVM and Clang packages from different majors: configure stops with
`gentest requires matching LLVM and Clang package majors`. While CMake loads
LLVM, it can print `Could NOT find` lines for optional libraries such as ZLIB
or LibXml2; they are harmless unless configure stops, in which case install
that library's development package.

## Build and install gentest

Run these commands from the folder where you keep source checkouts. The
install prefix is in your home folder, so you don't need administrator rights.

```bash
git clone https://github.com/jkammerland/gentest.git
gentest_source="$PWD/gentest"
gentest_prefix="$HOME/.local/gentest"
llvm_prefix="$(llvm-config --prefix)"
llvm_cmake_dir="$(llvm-config --cmakedir)"

cmake -S "$gentest_source" -B "$gentest_source/build/install" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$llvm_prefix/bin/clang" \
  -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" \
  -DLLVM_DIR="$llvm_cmake_dir" \
  -DClang_DIR="$llvm_cmake_dir/../clang" \
  -Dgentest_INSTALL=ON \
  -DCMAKE_INSTALL_PREFIX="$gentest_prefix"
cmake --build "$gentest_source/build/install" --parallel 2
cmake --install "$gentest_source/build/install"
"$gentest_prefix/bin/gentest_codegen" --help
```

This installs the headers, the runtime libraries, the CMake package and
`gentest_codegen`. The last command prints `OVERVIEW: gentest clang code
generator`, which shows that the tool and its LLVM libraries load.
`gentest_codegen --version` prints only LLVM's version; a test executable's
`--help` prints the gentest version on its first line (`gentest v<version>`).

Keep these points in mind:

- If CMake found an installed fmt, the gentest package requires exactly that
  fmt version, so your project must find the same fmt package. If the build
  downloaded fmt, it installs fmt into the gentest prefix.
- To build offline without an installed fmt, set the `GENTEST_FMT_SOURCE_DIR`
  environment variable (or the CMake variable `FETCHCONTENT_SOURCE_DIR_FMT`)
  to an fmt 12.1.0 source checkout.
- `gentest_codegen` loads the LLVM and Clang libraries from the installation it
  was built with, so keep that installation in place.
- Releases contain source archives, which you can build instead of a clone;
  [Packaging](../packaging.md) explains how to verify them. Prebuilt
  `gentest_codegen` binaries are tracked in
  [issue #136](https://github.com/jkammerland/gentest/issues/136).

## Write your first test project

You put annotated test declarations in headers that your test target lists or
includes, and `gentest_codegen` generates their registration at build time.
Define cases in a header as `inline` functions, not `static` and not in an
anonymous namespace. [Writing tests](../writing_tests.md) has the full rules.

Create a folder next to the gentest checkout:

```bash
mkdir gentest-first-test
cd gentest-first-test
```

Create `CMakeLists.txt`:

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

- `set(CMAKE_CXX_EXTENSIONS OFF)` compiles in ISO C++ mode. In GNU mode, GCC
  and Clang define `linux` as a macro, which breaks the `linux` tag (see
  [Writing tests](../writing_tests.md)).
- `find_package(gentest)` provides the `gentest::` targets and the CMake
  functions.
- `gentest::gentest_main` links the runtime and supplies `main()`.
- `gentest_attach_codegen()` runs `gentest_codegen` during the build and adds
  the generated sources to the target.
- `gentest_discover_tests()` registers each case as its own CTest test.

Create `cases.hpp`:

```cpp
#pragma once

#include "gentest/test.h"

[[gentest::test("demo/addition")]]
inline void addition() {
    const auto value = 2 + 2;
    gentest::expect_eq(value, 4, "addition result");
}
```

Configure in the same shell, which keeps `$llvm_prefix` and `$gentest_prefix`
from the previous step. Then build and run:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" \
  -DCMAKE_PREFIX_PATH="$gentest_prefix"
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/first_tests --list-tests
```

CTest runs one passing test named `demo/addition`, and `--list-tests` prints
the same name.

## See a failure and rerun one case

In `cases.hpp`, change the expected value to `5`:

```cpp
    gentest::expect_eq(value, 5, "addition result");
```

Rebuild and run only that case:

```bash
cmake --build build --parallel 2
./build/first_tests --run=demo/addition
```

The command exits with a nonzero status. The output looks like this, with
`<project>` standing for your project folder:

```text
[ FAIL ] demo/addition :: 1 issue(s) (0 ms)
EXPECT_EQ    failed at <project>/cases.hpp:8: addition result; lhs=4, rhs=5

Rerun: './build/first_tests' '--run=demo/addition' '--kind=test' '--no-color'
Summary: passed 0/1; failed 1; skipped 0; blocked 0; xfail 0; xpass 0.
Failed tests:
  demo/addition (cases.hpp:6):
    EXPECT_EQ    failed at <project>/cases.hpp:8: addition result; lhs=4, rhs=5
```

The `Rerun:` line runs just this case again. `--run` accepts any full name
that `--list-tests` prints; [Command-line options](../command_line.md) lists
the other options. To run the same case through CTest:

```bash
ctest --test-dir build -R '^demo/addition$' --output-on-failure
```

Change the expected value back to `4`, rebuild, and run CTest again to return
to a passing project.

## Keep definitions in a source file

A case defined in a header must be `inline`. To keep the definition in a
`.cpp` file instead, leave only the annotated declaration in the header.
Replace `cases.hpp` with:

```cpp
#pragma once

#include "gentest/attributes.h"

[[gentest::test("demo/addition")]]
void addition();
```

Add `cases.cpp`:

```cpp
#include "cases.hpp"

#include "gentest/test.h"

void addition() {
    const auto value = 2 + 2;
    gentest::expect_eq(value, 4, "addition result");
}
```

Change the target to `add_executable(first_tests cases.cpp cases.hpp)`, then
run the same configure, build and CTest commands. Keep the attribute on the
declaration in the header: codegen rejects gentest attributes in a `.cpp`
file.

## Try the examples

The gentest checkout contains small, complete projects that build against the
installed package:

| Example | What it shows |
| --- | --- |
| [Parameter matrices](../../examples/parameterized/) | Independent value axes, paired input/expected rows, and type/container matrices. |
| [Fixtures](../../examples/fixtures/) | Fresh local state, shared suite setup, and several fixture parameters inferred from a free function's signature. |
| [Mocks](../../examples/mocking/) | A mock target, argument matching, return values and call counts. |

For example, build the mocking example without changing it:

```bash
cmake -S "$gentest_source/examples/mocking" -B build-mocking -G Ninja \
  -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" \
  -DCMAKE_PREFIX_PATH="$gentest_prefix"
cmake --build build-mocking --parallel 2
ctest --test-dir build-mocking --output-on-failure
```

The example uses gentest mocks (`BACKEND gentest`, the default).
[Mocks](../mock_generation.md) also covers the gMock and Trompeloeil backends.
The [examples index](../../examples/README.md) lists every example.

## Install the runtime without the code generator

To install only the headers, runtime and CMake package, add
`-DGENTEST_BUILD_CODEGEN=OFF` to the configure command (with
`-Dgentest_INSTALL=ON`). This build doesn't need the LLVM and Clang
development packages.

Projects that use such an install must name a `gentest_codegen` that runs on
the build machine and comes from the same gentest version:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="$gentest_prefix" \
  -DGENTEST_CODEGEN_EXECUTABLE=/path/to/gentest_codegen
```

You can also set `GENTEST_CODEGEN_EXECUTABLE` in `CMakeLists.txt` after
`find_package(gentest)` and before `gentest_attach_codegen()` or
`gentest_add_mocks()`. `GENTEST_CODEGEN_TARGET` can name an executable target
instead; cross builds need an imported target. Without either, configure stops
with `gentest_attach_codegen: no gentest code generator available.`

## Named modules

Public named modules (`import gentest;`) are off by default. To install them,
add `-DGENTEST_ENABLE_PUBLIC_MODULES=ON` to the configure command, then build
and install again. Configure stops with an error if the toolchain can't build
them; `-DGENTEST_ENABLE_PUBLIC_MODULES=AUTO` turns them on only when it can.
[Named modules](../modules.md) covers `import gentest;`, cases in module
interface units (`MODULE_REGISTRATION`) and module mocks.

## Cross-compiling

A cross build can't run the `gentest_codegen` built for the target. Build
gentest for the build machine first, then pass that tool to the target build
with `-DGENTEST_CODEGEN_EXECUTABLE=<path>`. In cross builds, gentest builds no
code generator by default and never picks up an installed one.
[Host toolchains and cross-compiling](../buildsystems/host_toolchain_sysroots.md)
describes the full setup. [Cross-testing gentest under QEMU](../cross_compile/linux_qemu.md)
walks through a complete cross build of gentest's own tests.
