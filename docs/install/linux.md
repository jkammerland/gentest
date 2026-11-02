# Install and run your first test on Linux

This walkthrough builds Gentest from source, installs it in a user-owned prefix,
and runs a separate consumer. It uses ordinary C++ headers; named modules have a
separate [toolchain and setup guide](../modules.md).

## Requirements

| Component | Requirement |
| --- | --- |
| Application compiler | A C++20 compiler and standard library, including `std::stop_token`. Ordinary header-based tests can use Clang or GCC. |
| Build tools | CMake 3.31 or newer, Ninja, and Git. Both Ninja and Ninja Multi-Config are supported. |
| Code generator | Matching LLVM and Clang development packages, major version 19 or newer, plus a real host `clang++` executable and its builtin headers. The generator runs on the build host. |
| Formatting dependency | A CMake-discoverable fmt development package. If absent, the source build fetches fmt 12.1.0, which requires network access. |

The compiler that builds your application and the host Clang used by
`gentest_codegen` have separate roles. The commands below use one LLVM
installation for both to make the first build straightforward. Use a compatible
C++ runtime/standard-library ABI when linking the installed Gentest runtime into
your application. See [host toolchain and target sysroots](../buildsystems/host_toolchain_sysroots.md)
for separate compilers and cross builds.

Repository maintainers use LLVM 23 for formatting and static analysis. That lint
policy does not raise the LLVM/Clang 19 minimum for building the generator.
For module-authored tests, follow the [modules guide](../modules.md): it adds a
module-capable compiler, `clang-scan-deps`, Ninja 1.11 or newer, and an explicit
`GENTEST_ENABLE_PUBLIC_MODULES=ON` provider build.

Install the requirements with your distribution's package manager, or select an
existing LLVM installation. For example, Fedora calls the development packages
`llvm-devel`, `clang-devel`, and `fmt-devel`; Debian/Ubuntu use versioned
`llvm-<major>-dev` and `libclang-<major>-dev` packages and `libfmt-dev`.
Check that `llvm-config`, `clang`, and `clang++` select the same installation:

```bash
llvm-config --version
clang++ --version
cmake --version
ninja --version
```

If multiple LLVM versions are installed, put the chosen installation's `bin`
directory first on `PATH`, or replace `llvm-config` below with its versioned
executable. Do not mix LLVM and Clang CMake packages from different majors.

## Build and install Gentest

Run these commands from a directory where you keep source checkouts. The prefix
is explicit and needs no administrator privileges:

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
  -Dgentest_BUILD_TESTING=OFF \
  -DGENTEST_ENABLE_PUBLIC_MODULES=OFF \
  -DCMAKE_INSTALL_PREFIX="$gentest_prefix"
cmake --build "$gentest_source/build/install" --parallel 2
cmake --install "$gentest_source/build/install"
"$gentest_prefix/bin/gentest_codegen" --version
```

When CMake finds system fmt, the installed consumer also needs that fmt package.
If Gentest fetched fmt, its install rules place fmt in the same prefix. The
installed generator still needs the LLVM/Clang runtime libraries from the chosen
host installation. Portable codegen binary distribution is tracked in
[#136](https://github.com/jkammerland/gentest/issues/136).

## Write a header-only consumer

Before writing a case, choose its header. Ordinary textual code generation
registers **annotated declarations reachable through headers**. List a header as
a target source for a header-only target, or include it from an authored `.cpp`.
Generated registration sources include that header. Use `inline` for
non-template function definitions in it; split declarations and definitions are
shown below. Case declarations need external linkage, so keep them out of
anonymous namespaces and avoid `static`.

Create a new directory beside the Gentest checkout and enter it:

```bash
mkdir gentest-first-test
cd gentest-first-test
```

Create `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.31)
project(first_test LANGUAGES CXX)

include(CTest)
enable_testing()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(gentest CONFIG REQUIRED)

add_executable(first_tests cases.hpp)
target_compile_features(first_tests PRIVATE cxx_std_20)
target_link_libraries(first_tests PRIVATE gentest::gentest_main)

gentest_attach_codegen(first_tests)
gentest_discover_tests(first_tests)
```

`find_package` loads the codegen and CTest helpers. `gentest::gentest_main`
supplies the runtime and `main()`. The compile database records the consumer's
actual include paths, defines, and compiler options for the generator.

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

Configure with the prefix and compiler selected above, then build and run:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" \
  -DCMAKE_PREFIX_PATH="$gentest_prefix"
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/first_tests --list-tests --no-color
```

CTest reports one passing test; the inventory prints `demo/addition`.

## See a failure and rerun one case

In `cases.hpp`, change the expected value to `5`:

```cpp
    gentest::expect_eq(value, 5, "addition result");
```

Rebuild and rerun that exact case:

```bash
cmake --build build --parallel 2
./build/first_tests --run=demo/addition --no-color
```

The command returns a nonzero status and reports the case name, assertion source
location, actual value `4`, expected value `5`, and the message `addition result`.
The full name from `--list-tests` is accepted directly by `--run`. To select the
same case through CTest:

```bash
ctest --test-dir build -R '^demo/addition$' --output-on-failure
```

Restore the expected value to `4`, rebuild, and run CTest again to return to a
passing project.

## Split header and source

For out-of-line implementations, keep the attribute on the header declaration.
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

Change the target declaration to
`add_executable(first_tests cases.cpp cases.hpp)`, then run the same configure,
build, and CTest commands. The authored `.cpp` compiles normally alongside the
generated registration sources.

## Try matrices, fixtures, and mocks

The installed package also builds these small, complete consumer projects:

| Example | What to try |
| --- | --- |
| [Parameter matrices](../../examples/parameterized/) | Independent value axes, paired input/expected rows, and type/container matrices. |
| [Fixtures](../../examples/fixtures/) | Fresh local state, shared suite setup, and multiple fixture parameters inferred from a free-function signature. |
| [Generated mocks](../../examples/mocking/) | An explicit mock target, argument matching, return values, and call counts. |

For example, build the generated-mock walkthrough without changing its sources:

```bash
cmake -S "$gentest_source/examples/mocking" -B build-mocking -G Ninja \
  -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" \
  -DCMAKE_PREFIX_PATH="$gentest_prefix"
cmake --build build-mocking --parallel 2
ctest --test-dir build-mocking --output-on-failure
```

Use the default Gentest mock backend for Gentest's runner and reports. The
[mock-generation guide](../mock_generation.md) explains GMock and Trompeloeil
backends and their framework-specific execution/reporting.

## Cross-compiling

Build `gentest_codegen` for the host and select it with
`GENTEST_CODEGEN_EXECUTABLE` in the target build. Follow the complete
[Linux aarch64 QEMU](../cross_compile/linux_aarch64_qemu.md) or
[Linux riscv64 QEMU](../cross_compile/linux_riscv64_qemu.md) walkthrough for the
host/target build split and test execution.
