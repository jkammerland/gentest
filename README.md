# gentest

[![CI](https://github.com/jkammerland/gentest/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/jkammerland/gentest/actions/workflows/ci.yml)

gentest is a C++20 test framework. Tests are plain functions marked with
standard C++ attributes. At build time a Clang-based tool, `gentest_codegen`,
reads the attributes and writes the registration code, so you don't need
registration macros.

```cpp
#include "gentest/test.h"

#include <cstddef>
#include <list>
#include <vector>

using namespace gentest::asserts;

[[gentest::test]]
inline void basic() {
    EXPECT_TRUE(1 + 1 == 2);
}

// Two template axes in one attribute list: four cases (int or long, vector or list).
template <typename T, template <class...> class C>
[[using gentest: template(T, int, long), template(C, std::vector, std::list)]]
void emplace_matrix() {
    C<T> values;
    values.emplace_back(T{1});
    EXPECT_EQ(values.size(), std::size_t{1});
}
```

## Why gentest

- **No registration macros.** Tests, benchmarks, parameters, templates,
  fixture lifetimes and some metadata are all declared with attributes.
- **Matrices from one declaration.** Value axes, row tables and template
  parameters expand into one case per combination.
- **Fixtures from the signature.** Declare fixtures as function parameters,
  several per case. Each has a local, suite or global lifetime, which can be shared between tests in later cases.
- **Generated mocks.** `gentest_add_mocks()` generates mock classes for your
  types, MENTION IT CAN HANDLE ANY CLASS, EVEN NON-VIRTUAL, not other shit. You can say this helper also allows you to generate for gmock and the other.
- **More than pass or fail.** Benchmarks, jitter measurements and death tests
  are declared the same way.
- **Async tests** and fixture setups with C++20 coroutines.
- TODO: MAYBE THIS IS WEIRD? WHAT ARE THE USE CASE FOR THESE? WHY NOT CUSTOM/USER TAGS ONLY? **Fits your tooling.** Each case can be its own CTest test. Cases carry tags
  from a fixed set (`fast`, `slow`, `linux`, `windows`, `death`), requirement
  IDs and an owner. Runs can write JUnit XML, Allure results and GitHub Actions
  annotations. CMake, Meson, Xmake and Bazel are supported, as are named
  modules.

## Requirements

- CMake 3.31 or newer with the Ninja or Ninja Multi-Config generator, or one of
  the [other supported build systems](docs/buildsystems/downstream_consumers.md).
- A C++20 compiler and standard library
- Clang, GCC or MSVC. On macOS, AppleClang support starts at Xcode 26 / AppleClang 17.
- To build `gentest_codegen`: the LLVM and Clang development packages of one
  major version, 19 or newer. 

TODO: WHAT?? THIS NEEDS TO BE SIMPLIFIED.
To run it: the Clang of that LLVM version on the
  build machine, which codegen uses for Clang's built-in headers. Your tests
  can use a different compiler.

Named modules add [compiler and dependency-scanning requirements](docs/modules.md#requirements).

## Quick start

Install gentest first. You build it from source: the guides for
[Linux](docs/install/linux.md), [macOS](docs/install/macos.md) and
[Windows](docs/install/windows.md) list the packages, install gentest to a user
prefix and run a first project. Then build this small CMake project against
that install. On Windows, add the configure options the Windows guide shows
for your compiler.

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.31)
project(my_tests LANGUAGES CXX)

include(CTest)
# Compile as ISO C++20. In GNU mode, GCC and Clang define `linux` as a macro,
# which breaks the `linux` tag (and Clang module builds).
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(gentest CONFIG REQUIRED)

# The annotated header is the whole test target; codegen adds the generated
# registration sources to it.
add_executable(my_tests cases.hpp)
target_compile_features(my_tests PRIVATE cxx_std_20)
target_link_libraries(my_tests PRIVATE gentest::gentest_main)

gentest_attach_codegen(my_tests)
# One CTest test per case. To run every case in one process instead:
# add_test(NAME my_tests COMMAND my_tests)
gentest_discover_tests(my_tests)
```

`cases.hpp`:

```cpp
#pragma once

#include "gentest/test.h"

using namespace gentest::asserts;

[[using gentest: test("demo/addition"), fast]]
inline void addition() {
    const auto value = 2 + 2;
    gentest::expect_true(value == 4);
    EXPECT_EQ(value, 4);
}
```

Annotated declarations go in headers that the target lists or that its `.cpp`
files include; see [Writing tests](docs/writing_tests.md).

Configure, build and test, with `CMAKE_PREFIX_PATH` pointing at the gentest
install:

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the test executable directly for more control:

```bash
./build/my_tests                               # run every case except death tests
./build/my_tests --list                        # list cases with their tags and metadata
./build/my_tests --run=demo/addition           # run one case by full name or unique suffix
./build/my_tests --filter='demo/*' --tag=fast  # select by name pattern and tag
./build/my_tests --junit=junit.xml             # also write a JUnit report
./build/my_tests --help                        # show every option
```

[Command-line options](docs/command_line.md) describes every option and exit
code, and [Test selection](docs/test_selection.md) explains how the selectors
combine.

## Named modules

Cases can also live in named-module interface units that use
`import gentest;`. Build gentest with `-DGENTEST_ENABLE_PUBLIC_MODULES=ON` and
attach codegen with
`gentest_attach_codegen(<target> MODULE_REGISTRATION FILE_SET <name>)`.
[examples/hello_modules](examples/hello_modules) is a complete project, and
[Named modules](docs/modules.md) covers the requirements, what to export and
link, and module mocks.

## Documentation

The [docs index](docs/index.md) lists every page, including reference,
contributor and design-history pages.

### Write tests

| Page | What it covers |
| --- | --- |
| [Writing tests](docs/writing_tests.md) | Where cases live, attribute spelling, case names, tags and metadata, out-of-line definitions |
| [Assertions](docs/assertions.md) | `EXPECT_*` and `ASSERT_*` checks, exceptions, analyzer assertions, skip and xfail |
| [Failure diagnostics](docs/failure_diagnostics.md) | Scoped failure context, matchers, mock failure locations, libassert expression assertions |
| [Formatting values](docs/value_formatting.md) | How values are printed in failure messages |
| [Parameters and templates](docs/parameters.md) | Value axes, rows, numeric ranges, template matrices |
| [Fixtures](docs/fixtures.md) | Local, suite and global lifetimes, setup and teardown, activation order |
| [Fixture allocation hooks](docs/fixtures_allocation.md) | Custom construction and ownership with `gentest_allocate` |
| [Death tests](docs/death_tests.md) | Cases that must terminate the process, and how CTest runs them |
| [Mocks](docs/mock_generation.md) | Mock targets, mock definitions headers, gentest mocks, the gMock and Trompeloeil backends |
| [Logging and threads](docs/logging_and_threads.md) | `gentest::log()`, log sinks, worker threads, live output |
| [Benchmarks and jitter](docs/benchmarks.md) | Measured cases, timing reports, comparing two reports |
| [Runtime properties and structured records](docs/recording.md) | Facts and data recorded during a run, and their exports |
| [Named modules](docs/modules.md) | `import gentest;`, `MODULE_REGISTRATION`, module mocks, toolchain requirements |
| [Async tests](docs/async.md) (experimental) | Coroutine cases that return `gentest::awaitable<>` |

### Run tests

| Page | What it covers |
| --- | --- |
| [Command-line options](docs/command_line.md) | Every runner option, exit codes, environment variables |
| [Test selection](docs/test_selection.md) | Selecting by name, kind and tag; previews; empty selections |
| [Running, debugging, and replaying tests](docs/runner_workflows.md) | Rerun commands, debugger breaks, captured output, shuffle replay, CTest workflows |
| [CTest integration](docs/discover_tests.md) | `gentest_discover_tests()` and its options |
| [Reports](docs/reports.md) | JUnit, Allure, GitHub annotations, how outcomes map to each |
| [Test inventory](docs/test_inventory.md) | `--list-json` output for IDEs, dashboards and CI tools |
| [Requirements traceability](docs/traceability_standards.md) | Requirement-to-test trace matrices from `req(...)` |
| [Notifications and progress watchdogs](docs/notifications.md) | Progress notifications, watchdogs, framework events |

### Integrate with your build

| Page | What it covers |
| --- | --- |
| [Install gentest on Linux](docs/install/linux.md) | Build and install gentest from source, then run a first project |
| [Install gentest on macOS](docs/install/macos.md) | The same with Homebrew LLVM, which codegen always needs; tests can also build with AppleClang 17 (Xcode 26) |
| [Install gentest on Windows](docs/install/windows.md) | The same with an LLVM release package, using Clang or MSVC |
| [Choosing a build system](docs/buildsystems/downstream_consumers.md) | Support matrix and settings for CMake, Meson, Xmake and Bazel |
| [Meson](docs/buildsystems/meson.md) | gentest as a Meson wrap or subproject, without named modules |
| [Xmake](docs/buildsystems/xmake.md) | An installed gentest from Xmake, with the installed helper functions |
| [Bazel](docs/buildsystems/bazel.md) | The Bzlmod source package and its codegen toolchain |
| [Host toolchains and cross-compiling](docs/buildsystems/host_toolchain_sysroots.md) | Host Clang selection, sysroots, cross builds of your project |
| [Embedding the runner](docs/runner_api.md) | Your own `main()` (`gentest::run_all_tests`), hand-written cases (`gentest::Case`, `gentest::run_cases`) |
| [Packaging](docs/packaging.md) | Release archives and signatures, Common Package Specification (CPS) and software bill of materials (SBOM) metadata, installed package helper |
| [Windows troubleshooting](docs/windows_troubleshooting.md) | LLVM discovery and codegen problems on Windows |

Release notes are in [CHANGELOG.md](CHANGELOG.md), and removed or deprecated
features with their replacements are in [DEPRECATIONS.md](DEPRECATIONS.md).

## Examples

The [examples](examples/README.md) are small CMake projects that build against
an installed gentest:

- [examples/hello_header_only](examples/hello_header_only): a test target whose only source is a header.
- [examples/hello](examples/hello): declarations in a header, definitions in a `.cpp`.
- [examples/hello_modules](examples/hello_modules): cases in a named module that uses `import gentest;`.
- Feature walkthroughs: [parameters](examples/parameterized/), [fixtures](examples/fixtures/),
  [mocks](examples/mocking/), [benchmarks and jitter](examples/measured/),
  [recording](examples/recording/), [metadata](examples/metadata/) and [async](examples/async/).

## Cross-compiling

`gentest_codegen` runs on the build machine, so a cross build needs a host
build of it; in CMake, pass its path as `-DGENTEST_CODEGEN_EXECUTABLE=<path>`.
See [Host toolchains and cross-compiling](docs/buildsystems/host_toolchain_sysroots.md).

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) covers building this repository, tests,
lint, static analysis, coverage, CI and pull requests.

## License

gentest is licensed under the [Boost Software License 1.0](LICENSE). Release
archives, their signatures, and the optional CPS and SBOM metadata are
described in [Packaging](docs/packaging.md).
