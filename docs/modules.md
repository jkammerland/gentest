# gentest modules guide

This page is the module-first counterpart to the root [README](../README.md). It focuses on `import gentest;`, named-module test sources, and module-local mocks.

## What is supported

Public named modules:
- `import gentest;`
- `import gentest.mock;`
- `import gentest.bench_util;`

Supported module-authored test flows:
- tests, benches, and jitters in named modules
- suite/global fixtures declared in named modules
- mocks of types defined in headers or named modules
- package-consumer builds that use the installed public modules

## Requirements

- CMake >= 3.31
- C++20 compiler with named-module support
- LLVM/Clang available for `gentest_codegen`
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON`

Per-TU wrapper mode for module sources requires a single-config generator/build directory. For multi-config generators, use manifest mode with `gentest_attach_codegen(... OUTPUT ...)`.

## CMake quick start

```cmake
include(CTest)
enable_testing()

find_package(gentest CONFIG REQUIRED)

add_executable(my_tests
  main.cpp
  cases.cppm)

target_link_libraries(my_tests PRIVATE
  gentest::gentest
  gentest::gentest_main)

gentest_attach_codegen(my_tests)
gentest_discover_tests(my_tests)
```

If you provide your own `main()`, link `gentest::gentest_runtime` instead of `gentest::gentest_main`.

## Minimal layout

`main.cpp`:

```cpp
import gentest;
import my.tests;

auto main(int argc, char** argv) -> int {
    return gentest::run_all_tests(argc, argv);
}
```

`cases.cppm`:

```cpp
module;

#include <memory>

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#include "gentest/bench_util.h"
#endif

export module my.tests;

import gentest;
import gentest.bench_util;
import gentest.mock;

using namespace gentest::asserts;

export namespace demo {

struct Service {
    virtual ~Service() = default;
    virtual int compute(int arg) = 0;
};

struct [[using gentest: fixture(suite)]] SuiteFixture : gentest::FixtureSetup {
    void setUp() override { value = 7; }
    int value = 0;
};

[[using gentest: test("demo/module_test")]]
void module_test(SuiteFixture& suite_fx) {
    EXPECT_EQ(suite_fx.value, 7);
}

[[using gentest: test("demo/module_mock")]]
void module_mock() {
    gentest::mock<Service> mock_service;
    gentest::expect(mock_service, &Service::compute).times(1).with(3).returns(9);

#if !defined(GENTEST_CODEGEN)
    Service* service = &mock_service;
    EXPECT_EQ(service->compute(3), 9);
#endif
}

[[using gentest: bench("demo/module_bench"), baseline]]
void module_bench(SuiteFixture& suite_fx) {
    gentest::doNotOptimizeAway(suite_fx.value);
}

} // namespace demo
```

## Why the `GENTEST_CODEGEN` block exists

This block is not required for every module source.

Use it only when the codegen parse path needs the header-backed API in the global module fragment. The common cases are:

- `gentest/mock.h` for direct mock support
- `gentest/bench_util.h` for helpers like `gentest::doNotOptimizeAway`

Why: `gentest_attach_codegen()` parses your sources with `GENTEST_CODEGEN` defined before the final generated wrappers exist. In that parse mode, some module flows still need the header form to be available from the global module fragment.

Normal builds still use the public modules:

```cpp
import gentest;
import gentest.mock;
import gentest.bench_util;
```

So the practical rule is:

- plain module tests that only use `import gentest;`, assertions, fixtures, and `run_all_tests()` usually do not need this block
- module tests that directly use `gentest::mock<T>` generally should keep the codegen-only `gentest/mock.h` include
- module tests that use `gentest::doNotOptimizeAway` or `gentest::clobberMemory` can add the codegen-only `gentest/bench_util.h` include if their codegen parse path does not already see those helpers through imports

For mocks, define `GENTEST_NO_AUTO_MOCK_INCLUDE` around the codegen-only `#include "gentest/mock.h"` as shown above. That keeps the global fragment explicit and avoids the legacy auto-include path.

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

Normal module builds only need `import gentest.mock;` for the public API surface. The generated code handles the target-local specialization attachment:

- header-defined mocks are emitted into the header-domain generated shard
- module-defined mocks are emitted into one generated shard per named module
- generated module wrappers inject the required attachment code automatically

You do not need to manually include `gentest/mock_codegen.h` in normal module sources.

## Practical notes

- `gentest_codegen` uses your active compilation database. If module imports fail during codegen, check the target's `compile_commands.json` first.
- `import gentest;` consumers should link `gentest::gentest`, not just `gentest::gentest_main`.
- If you need background on how the module flow was built and hardened, see:
  - [docs/stories/009_public_modules_end_to_end.md](stories/009_public_modules_end_to_end.md)
  - [docs/stories/010_public_modules_progress_report.md](stories/010_public_modules_progress_report.md)
