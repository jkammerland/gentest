# gentest

`gentest` experiments with attribute-driven test discovery for C++ projects. Test entry points rely on standard C++
attributes (`[[using gentest : ...]]`), then a clang-tooling generator materialises the runtime glue that invokes every
discovered function through a single `gentest::run_all_tests` entry-point.

## What's in the tree?
- `include/gentest/runner.h` – lightweight assertion helpers and the declaration of `gentest::run_all_tests`.
- `include/gentest/attributes.h` – guidance for annotating tests using
  `[[using gentest : ...]]` attributes (no vendor macros required).
- `tools/gentest_codegen` – the clang-based manifest generator that scans sources and emits `test_impl.cpp` files.
- `cmake/GentestCodegen.cmake` – helper that wires code-generation results into any CMake target through the
  `gentest_attach_codegen()` function.
- `tests/` – two suites (`unit` and `integration`) that demonstrate codegen-driven execution through the shared
  `gentest::run_all_tests` harness.

## Local workflow
```bash
cmake --preset=debug
cmake --build --preset=debug
ctest --preset=debug --output-on-failure
```

`gentest_codegen` relies on clang libraries and the active build’s `compile_commands.json`. vcpkg users can install the
`llvm[clang,clang-tools-extra]` port, and everyone else should ensure `clang++` is on `PATH` together with the host
libstdc++ headers (e.g. `/usr/lib/gcc/x86_64-redhat-linux/15`).

## Authoring new test suites
1. Create a test source (e.g. `tests/widgets/cases.cpp`) and tag functions with
   namespaced attributes understood by the generator:
   ```c++
   [[using gentest : test("widgets/basic/health"), slow, linux, req("BUG-42")]]
   void widget_smoke() {
       gentest::expect_eq(create_widget().status(), widget_status::healthy);
   }
   ```
   The interface target `${PROJECT_NAME}` exports the right warning suppressions to
   consumers (e.g. `-Wno-unknown-attributes` on Clang/GCC, `/wd5030` on MSVC), so no
   local `#pragma` blocks are required.
2. Add a target in `tests/CMakeLists.txt` and call `gentest_attach_codegen()` to hook the generator:
   ```cmake
   add_executable(gentest_widgets_tests support/test_entry.cpp widgets/cases.cpp)
   gentest_attach_codegen(gentest_widgets_tests
       OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/widgets/test_impl.cpp
       SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/widgets/cases.cpp)
   add_test(NAME widgets COMMAND gentest_widgets_tests)
   ```
   The helper injects a custom command that runs `gentest_codegen`, consumes the active `compile_commands.json`, and
   appends the generated `test_impl.cpp` to the target sources.

`gentest_codegen` also supports a lint-only mode to validate attributes without emitting code:
```bash
$ ./build/tools/gentest_codegen --check --compdb ./build/debug -- -std=c++23 tests/unit/cases.cpp
```

Running the resulting binary executes every annotated function, printing `[ PASS ]` / `[ FAIL ]` lines and returning a
non-zero exit status when any test throws `gentest::failure`. Pass `--list` to enumerate discovered cases and inspect
their metadata: the generator includes tags, requirement IDs, and skip markers that originate from the `[[using
gentest : ...]]` attribute list.

See [`AGENTS.md`](AGENTS.md) for contribution guidelines and additional workflow conventions.
