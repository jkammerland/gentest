# gentest

`gentest` is an attribute-driven C++ test runner + code generator.

Write tests using standard C++ attributes (`[[using gentest: ...]]`). The build runs `gentest_codegen` to generate
`test_impl.cpp`, then your test binary runs `gentest::run_all_tests`.

>[!NOTE]
> This README is intentionally minimal. The previous (full) README snapshot is archived at
> [`docs/archive/README_2026-01-03.md`](docs/archive/README_2026-01-03.md).
> Start at [`docs/index.md`](docs/index.md) for the rest of the docs.

## Requirements

- CMake ≥ 3.31
- C++23 compiler (this repo is built/tested as C++23)
- LLVM/Clang (for `gentest_codegen`)

>[!IMPORTANT]
> `gentest_codegen` consumes your build’s `compile_commands.json` (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`).

## Build & run this repo

```bash
cmake --preset=debug-system
cmake --build --preset=debug-system
ctest --preset=debug-system --output-on-failure
```

## Use in your project (CMake)

`cases.cpp`:

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"
using namespace gentest::asserts;

namespace [[using gentest: suite("demo")]] demo {

[[using gentest: test("basic")]]
void basic() { EXPECT_TRUE(1 + 1 == 2); }

} // namespace demo
```

`main.cpp`:

```cpp
#include "gentest/runner.h"

int main(int argc, char** argv) { return gentest::run_all_tests(argc, argv); }
```

`CMakeLists.txt`:

```cmake
add_executable(my_tests main.cpp)
target_link_libraries(my_tests PRIVATE gentest::gentest)
gentest_attach_codegen(my_tests
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test_impl.cpp
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp)
add_test(NAME my_tests COMMAND my_tests)
```

Run:

```bash
./my_tests --list-tests
./my_tests
```

>[!WARNING]
> Cross-compiling requires a *host* `gentest_codegen` executable. See
> [`docs/cross_compile/linux_aarch64_qemu.md`](docs/cross_compile/linux_aarch64_qemu.md) and the install templates under
> [`docs/install/`](docs/install/).

## Docs

- Docs index: [`docs/index.md`](docs/index.md)
- Install templates: [`Linux`](docs/install/linux.md), [`macOS`](docs/install/macos.md), [`Windows`](docs/install/windows.md)
