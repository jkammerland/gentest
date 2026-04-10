# gentest modules guide

This page is the module-first counterpart to the root [README](../README.md). It focuses on `import gentest;`, named-module test sources, and the current explicit mock-target flow for module-defined types.

## What is supported

Public named modules:
- `import gentest;`
- `import gentest.mock;`
- `import gentest.bench_util;`

Supported module-authored test flows:
- tests, benches, and jitters in named modules
- suite/global fixtures declared in named modules
- clean additive testcase registration with `gentest_register_module_tests(...)`
- explicit mock targets for types defined in headers or named modules
- package-consumer builds that use the installed public modules

## Requirements

- CMake >= 3.31
- C++20 compiler with named-module support
- LLVM/Clang available for `gentest_codegen`
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON`
- single-config generator/build directory for clean module registration
- single-config generator/build directory for explicit module mock targets
- installed-package consumers also need the exact matching `fmt` CMake package
  discoverable, typically through the same `CMAKE_PREFIX_PATH` as `gentest`

For host-toolchain vs target-sysroot setup details, including cross-build
examples, see [buildsystems/host_toolchain_sysroots.md](buildsystems/host_toolchain_sysroots.md).

There are three module-codegen paths today:

- clean additive testcase registration via `gentest_register_module_tests(...)`
- the current explicit-mock testcase path via plain `gentest_attach_codegen(...)`
- legacy manifest mode via `gentest_attach_codegen(... OUTPUT ...)`

For multi-config generators, clean additive registration is not supported. Use
a dedicated single-config build directory, or fall back to manifest mode via
`gentest_attach_codegen(... OUTPUT ...)`.

## Clean registration quick start

Use `gentest_register_module_tests(...)` for named-module testcase file sets.
The authored module source stays in your `FILE_SET CXX_MODULES`; gentest adds
private same-module implementation units under the build tree for registration.

```cmake
include(CTest)
enable_testing()

find_package(gentest CONFIG REQUIRED)

add_executable(my_tests
  main.cpp)

target_sources(my_tests
  PRIVATE
    FILE_SET module_cases TYPE CXX_MODULES FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/cases.cppm)

target_link_libraries(my_tests PRIVATE
  gentest::gentest
  gentest::gentest_runtime)

gentest_register_module_tests(my_tests
  FILE_SET module_cases)
gentest_discover_tests(my_tests)
```

If you do not provide your own `main()`, link `gentest::gentest_main` instead of `gentest::gentest_runtime`.

`main.cpp`:

```cpp
#include "gentest/runner.h"

auto main(int argc, char** argv) -> int {
    return gentest::run_all_tests(argc, argv);
}
```

`cases.cppm`:

```cpp
export module my.tests;

import gentest;
import gentest.bench_util;

using namespace gentest::asserts;

export namespace demo {

struct [[using gentest: fixture(suite)]] SuiteFixture : gentest::FixtureSetup {
    void setUp() override { value = 7; }
    int value = 0;
};

[[using gentest: test("demo/module_test")]]
void module_test(SuiteFixture& suite_fx) {
    EXPECT_EQ(suite_fx.value, 7);
}

[[using gentest: bench("demo/module_bench"), baseline]]
void module_bench(SuiteFixture& suite_fx) {
    gentest::doNotOptimizeAway(suite_fx.value);
}

[[using gentest: jitter("demo/module_jitter")]]
void module_jitter(SuiteFixture& suite_fx) {
    gentest::doNotOptimizeAway(suite_fx.value);
}

} // namespace demo
```

### Clean registration rules

- `gentest_register_module_tests(...)` requires an explicit `FILE_SET` name
- the selected file set must contain primary named-module interface units
- partition units are rejected in the clean path
- global module fragments (`module;`) are rejected in the clean path
- private module fragments (`module :private;`) are rejected in the clean path
- each testcase module must directly `import gentest;`
- `gentest_register_module_tests(...)` cannot be combined with `gentest_attach_codegen()` on the same target

## Explicit module mocks

Module testcase registration is clean now, but explicit module mocks still use
the current explicit mock-target flow. That means:

- explicit mock targets are still created with `gentest_add_mocks(...)`
- testcase targets that consume those explicit mock targets still use
  `gentest_attach_codegen(...)` today so codegen can see the generated mock
  surface during discovery
- clean additive registration via `gentest_register_module_tests(...)` does not
  currently compose with explicit mock targets on the same testcase target
- explicit module mock targets also require a single-config generator/build
  directory today

If a testcase target started on `gentest_register_module_tests(...)` and later
needs explicit mocks, replace that target's clean registration call with
`gentest_attach_codegen(...)`.

Example:

`service.cppm`:

```cpp
export module my.service;

export namespace demo {

struct Service {
    virtual ~Service() = default;
    virtual int compute(int arg) = 0;
};

} // namespace demo
```

`mock_defs.cppm`:

```cpp
export module my.mock_defs;

import gentest.mock;

export import my.service;

export namespace demo::mocks {

using ServiceMock = gentest::mock<demo::Service>;

} // namespace demo::mocks
```

`cases.cppm`:

```cpp
export module my.tests;

import gentest;
import my.service_mocks;

using namespace gentest::asserts;

export namespace demo {

[[using gentest: test("demo/module_mock")]]
void module_mock() {
    demo::mocks::ServiceMock mock_service;
    gentest::expect(mock_service, &Service::compute).times(1).with(3).returns(9);
    Service* service = &mock_service;
    EXPECT_EQ(service->compute(3), 9);
}

} // namespace demo
```

Add the mock target in CMake:

```cmake
gentest_add_mocks(my_service_mocks
  DEFS
    ${CMAKE_CURRENT_SOURCE_DIR}/service.cppm
    ${CMAKE_CURRENT_SOURCE_DIR}/mock_defs.cppm
  OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
  MODULE_NAME my.service_mocks)

add_executable(my_tests
  main.cpp
  cases.cppm)

target_link_libraries(my_tests PRIVATE
  gentest::gentest
  gentest::gentest_runtime
  my_service_mocks)

gentest_attach_codegen(my_tests)
gentest_discover_tests(my_tests)
```

Link explicit mock targets before `gentest_attach_codegen()`, so codegen sees
the generated mock surface during its first parse.

The important rule is simple:

- header defs stay header-based and publish a generated header
- module defs stay module-based and publish a generated module
- module test sources do not need `#if defined(GENTEST_CODEGEN)` bootstrap blocks for direct mocks anymore

## Assertions and exceptions

Include-based consumers can use gtest-like macros. Module consumers should use the matching function templates from `gentest::asserts`.

```cpp
import gentest;
using namespace gentest::asserts;

[[gentest::test("exceptions/module_functions")]]
void module_functions() {
    EXPECT_THROW<std::runtime_error>([] { throw std::runtime_error("boom"); });
    EXPECT_NO_THROW([] {});
}
```

For richer checks, use normal C++ plus `gentest::fail(...)` or `gentest::expect(...)`.

## Module-local mocks

Use an explicit mock target and import its generated module surface:

```cpp
import my.service_mocks;

demo::mocks::ServiceMock service;
gentest::expect(service, &demo::Service::compute).times(1).with(3).returns(9);
```

Prefer the aliases and helper surface exported by the mock module itself.

## Practical notes

- `gentest_codegen` uses your active compilation database. If module imports fail during codegen, check the target's `compile_commands.json` first.
- `import gentest;` consumers should link `gentest::gentest`, not just `gentest::gentest_main`.
- Clean module registration is intended for explicitly selected testcase file sets, not every `CXX_MODULES` file set on a target.
- If you need background on how the module flow was built and hardened, see:
  - [docs/stories/009_public_modules_end_to_end.md](stories/009_public_modules_end_to_end.md)
  - [docs/stories/010_public_modules_progress_report.md](stories/010_public_modules_progress_report.md)
