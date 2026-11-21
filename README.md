# gentest

[![CI](https://github.com/jkammerland/gentest/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/jkammerland/gentest/actions/workflows/ci.yml)

gentest is a C++20 test framework. Tests are plain functions marked with
standard C++ attributes. At build time a Clang-based tool, `gentest_codegen`,
reads the attributes and writes the registration code.

```cpp
#include "gentest/bench_util.h"
#include "gentest/test.h"
#include "public/payment_mocks.hpp"

#include <cmath>
#include <cstddef>
#include <list>
#include <numbers>
#include <vector>

using namespace gentest::asserts;

namespace containers {

[[gentest::test]]
inline void starts_empty() {
    const std::vector<int> values;
    EXPECT_TRUE(values.empty());
}

// Two template axes in one attribute list: four cases (int or long, vector or list).
template <typename T, template <class...> class C>
[[using gentest: template(T, int, long), template(C, std::vector, std::list)]]
void emplace_matrix() {
    C<T> values;
    values.emplace_back(T{1});
    EXPECT_EQ(values.size(), std::size_t{1});
}

} // namespace containers

namespace payments {

[[gentest::test("payment_succeeds")]]
inline void paymentSucceeds() {
    shop::mocks::PaymentGatewayMock gateway;
    gentest::expect(gateway, &shop::PaymentGateway::charge).times(1).with(250).returns(true);

    shop::Checkout checkout(gateway);
    EXPECT_TRUE(checkout.pay(250));
}

} // namespace payments

namespace trig {

struct SineInput {
    double angle = 0.0;

    double nextAngle() {
        constexpr double full_turn = 2.0 * std::numbers::pi;
        angle += 0.01;
        if (angle >= full_turn) {
            angle -= full_turn;
        }
        return angle;
    }
};

[[gentest::bench("sine_throughput")]]
inline void sineThroughput(SineInput &input) {
    gentest::doNotOptimizeAway(std::sin(input.nextAngle()));
}

[[gentest::jitter("sine_latency")]]
inline void sineLatency(SineInput &input) {
    gentest::doNotOptimizeAway(std::sin(input.nextAngle()));
}

} // namespace trig
```

The [mocking example](examples/mocking/) shows how to generate `public/payment_mocks.hpp`.

## Why gentest

- **No registration macros.** Tests, benchmarks, parameters, templates,
  fixture lifetimes and declared metadata use attributes.
- **Matrices from one declaration.** Value axes, row tables and template
  parameters expand into one case per combination.
- **Fixtures from the signature.** Declare fixtures as function parameters,
  several per case. Local fixtures are fresh for each case; suite and global
  fixtures are shared, with optional setup and teardown.
- **Generated mocks.** Mock classes with virtual or non-virtual methods,
  including static members and member templates. See [Mocks](docs/mock_generation.md).
- **More than pass or fail.** Declare benchmarks, jitter measurements and
  death tests with attributes.
- **Async tests.** Write tests and fixture setup with C++20 coroutines.
  [Async support](docs/async.md) is experimental.
- **Fits your tooling.** Select cases by name or user-defined tag. Runs can
  write JUnit XML, Allure results and GitHub Actions annotations.

## Requirements

- A C++20 compiler such as Clang, GCC, MSVC or AppleClang.
- LLVM/Clang 19 or newer on the build machine for code generation.
- CMake 3.31 or newer, or
  [Meson, Xmake or Bazel](docs/buildsystems/downstream_consumers.md).

For named modules, see the [module requirements](docs/modules.md#requirements).

## Quick start

Install gentest from source on [Linux](docs/install/linux.md),
[macOS](docs/install/macos.md) or [Windows](docs/install/windows.md).
To build for another machine, start with [Cross-compiling](#cross-compiling).
For a native build, create these two files in a new directory.

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.31)
project(my_tests LANGUAGES CXX)

include(CTest)
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

namespace demo {

[[using gentest: test, tag("fast")]]
inline void addition() {
    const auto value = 2 + 2;
    EXPECT_EQ(value, 4);
}

} // namespace demo
```

Annotated declarations go in headers that the target lists or that its `.cpp`
files include; see [Writing tests](docs/writing_tests.md).

The checks also have lowercase names, such as `gentest::expect_eq` for
`EXPECT_EQ`; see [Assertions](docs/assertions.md#check-values).

Point `CMAKE_PREFIX_PATH` at your gentest installation, then build and run the test.

> [!NOTE]
> On Windows, use the [configure command for your compiler](docs/install/windows.md#write-your-first-test-project).

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

### Cross-compiling

Run `gentest_codegen` on the build machine and compile the runtime and your
tests with the target toolchain. The [Arm Linux walkthrough](docs/buildsystems/host_toolchain_sysroots.md#cross-compile-a-cmake-project)
runs the tests under QEMU. See also [QNX toolchains](docs/buildsystems/host_toolchain_sysroots.md#qnx-toolchains).

## Named modules

You can export test cases from a C++ module. The [module example](examples/hello_modules)
uses `import gentest;`. See [Named modules](docs/modules.md) for setup.

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
| [Death tests](docs/death_tests.md) | Check that a test terminates its process |
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
| [Install gentest on macOS](docs/install/macos.md) | Build with LLVM Clang or AppleClang |
| [Install gentest on Windows](docs/install/windows.md) | Build with Clang or MSVC |
| [Choosing a build system](docs/buildsystems/downstream_consumers.md) | Support matrix and settings for CMake, Meson, Xmake and Bazel |
| [Meson](docs/buildsystems/meson.md) | Use gentest as a Meson wrap or subproject |
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

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) covers building this repository, tests,
lint, static analysis, coverage, CI and pull requests.

## License

gentest is licensed under the [Boost Software License 1.0](LICENSE). Release
archives, their signatures, and the optional CPS and SBOM metadata are
described in [Packaging](docs/packaging.md).
