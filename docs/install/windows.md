# Install gentest on Windows

This page takes you from source to a passing test in your own CMake project on
Windows. You build gentest with LLVM's Clang or with MSVC, install it into a
folder you own, and run a first test. [Install gentest on Linux](linux.md)
explains each step in more detail, and
[Windows troubleshooting](../windows_troubleshooting.md) covers common errors.

## Requirements

| Component | Requirement |
| --- | --- |
| Visual Studio | Visual Studio 2022 with the Desktop development with C++ workload. Both Clang and MSVC builds use its libraries and the Windows SDK. |
| Code generator | The LLVM archive `clang+llvm-<version>-x86_64-pc-windows-msvc.tar.xz` from the [LLVM releases](https://github.com/llvm/llvm-project/releases), version 19 or newer. It contains the CMake packages and libraries that `gentest_codegen` links. |
| Build tools | CMake 3.31 or newer, Ninja, and Git. |
| Compiler for your tests | LLVM's `clang++` or MSVC's `cl`. |

Build gentest and your project in `RelWithDebInfo` (or `Release`). Prebuilt
LLVM packages contain release-mode libraries, and gentest's Windows tests run
in `RelWithDebInfo`.

## Prepare a developer shell

Run every command on this page in one 64-bit Developer PowerShell. This
snippet turns a plain PowerShell window into one:

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
& "$vsPath\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
```

Pass `-Arch amd64`, because the script targets x86 by default. Don't run
`VsDevCmd.bat` from PowerShell: it runs in a child `cmd.exe`, and the
environment it sets up is lost when it exits.

Extract the LLVM archive, for example to `C:\Tools`, and put its `bin` folder
first on `PATH`. `gentest_codegen` looks for `clang++.exe` there when it
needs a host Clang.

```powershell
$llvm = 'C:\Tools\clang+llvm-23.1.0-x86_64-pc-windows-msvc'
$env:PATH = "$llvm\bin;$env:PATH"
```

The LLVM 23 archive also needs zlib, zstd and libxml2: CMake must find them
when it loads LLVM, and `gentest_codegen.exe` must find their DLLs when it
runs. You can install them with a standalone vcpkg. Run it outside the gentest
checkout, whose `vcpkg.json` would switch vcpkg to manifest mode.

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\Tools\vcpkg
& C:\Tools\vcpkg\bootstrap-vcpkg.bat -disableMetrics
& C:\Tools\vcpkg\vcpkg.exe install zlib:x64-windows zstd:x64-windows libxml2:x64-windows
$deps = 'C:\Tools\vcpkg\installed\x64-windows'
$env:CMAKE_PREFIX_PATH = $deps
$env:PATH = "$deps\bin;$env:PATH"
```

Check the tools. `cl` prints its banner, which should end in `for x64`.

```powershell
cl
clang++ --version
cmake --version
ninja --version
```

The developer shell can put Visual Studio's own CMake and Ninja first on
`PATH`. If its CMake is older than 3.31, put a newer one ahead of it.

## Build and install gentest

```powershell
git clone https://github.com/jkammerland/gentest.git
$gentestPrefix = 'C:\Tools\gentest'

cmake -S gentest -B gentest\build\install -G Ninja `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_C_COMPILER="$llvm\bin\clang.exe" `
  -DCMAKE_CXX_COMPILER="$llvm\bin\clang++.exe" `
  -DLLVM_DIR="$llvm\lib\cmake\llvm" `
  -DClang_DIR="$llvm\lib\cmake\clang" `
  -Dgentest_INSTALL=ON `
  -DCMAKE_INSTALL_PREFIX="$gentestPrefix"
cmake --build gentest\build\install
cmake --install gentest\build\install
& "$gentestPrefix\bin\gentest_codegen.exe" --help
```

The last command prints `OVERVIEW: gentest clang code generator`. A test
executable's `--help` prints the installed gentest version on its first line
(`gentest v<version>`). To build
with MSVC instead, pass `-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl`.
`gentest_codegen` links LLVM with either compiler, so keep `LLVM_DIR` and
`Clang_DIR`.

The two compilers produce libraries with different C runtime settings:

- With Clang, gentest uses the static release C runtime (`MultiThreaded`) and
  `_ITERATOR_DEBUG_LEVEL=0` everywhere, the settings of the prebuilt LLVM
  libraries.
- With MSVC, only `gentest_codegen.exe` uses those settings. The installed
  libraries use CMake's default C runtime.

Your project must match the settings of the gentest install it links, as the
next section shows.

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

From that folder, configure with the compiler you used for gentest. For a
gentest install built with Clang, select the same C runtime:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_CXX_COMPILER="$llvm\bin\clang++.exe" `
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
  "-DCMAKE_CXX_FLAGS_INIT=-D_ITERATOR_DEBUG_LEVEL=0 -D_HAS_ITERATOR_DEBUGGING=0" `
  -DCMAKE_PREFIX_PATH="$gentestPrefix"
```

For a gentest install built with MSVC, the defaults match:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_CXX_COMPILER=cl `
  -DCMAKE_PREFIX_PATH="$gentestPrefix"
```

Then build and run the tests:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

`gentest_codegen.exe` runs during the build, so the shell needs `$llvm\bin`
on `PATH`, and for LLVM 23 also `$deps\bin`. CTest runs one passing test named
`demo/addition`.

## See a failure and rerun one case

Change the expected value in `cases.hpp` to `5`, rebuild, and run only that
case:

```powershell
cmake --build build
.\build\first_tests.exe --run=demo/addition
```

The output shows the failed assertion and a `Rerun:` command for this case;
[See a failure and rerun one case](linux.md#see-a-failure-and-rerun-one-case)
shows it in full.

## Next steps

The rest of the Linux guide applies on Windows too:

- [Keep definitions in a source file](linux.md#keep-definitions-in-a-source-file)
- [Try the examples](linux.md#try-the-examples): pass the same configure
  options as for your project.
- [Install the runtime without the code generator](linux.md#install-the-runtime-without-the-code-generator)
- [Named modules](linux.md#named-modules): they also need Ninja 1.11 or newer.

[Writing tests](../writing_tests.md) explains how to write cases, and
[Command-line options](../command_line.md) lists the runner options. To build
and run gentest's own tests, see [Contributing](../../CONTRIBUTING.md).
