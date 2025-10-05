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
 - The generated runtime uses `fmt::print` for output; CMake links `fmt::fmt` to test targets automatically.

## Local workflow
System toolchain (LLVM/Clang 20+ installed via your package manager):
```bash
cmake --preset=debug-system
cmake --build --preset=debug-system
ctest --preset=debug-system --output-on-failure
```

Legacy vcpkg-based toolchain (downloads and builds its own LLVM):
```bash
cmake --preset=debug
cmake --build --preset=debug
ctest --preset=debug --output-on-failure
```

If LLVM/Clang is installed somewhere other than `/usr/local/lib/cmake/{llvm,clang}`, set
`LLVM_DIR` and `Clang_DIR` before configuring, for example:
```bash
cmake --preset=debug-system -DLLVM_DIR="$(llvm-config --cmakedir)" -DClang_DIR=/opt/llvm/lib/cmake/clang
```

`gentest_codegen` relies on clang libraries and the active build’s `compile_commands.json`. vcpkg users can install the
`llvm[clang,clang-tools-extra]` port plus `fmt` (pulled automatically for tests), and everyone else should ensure
`clang++` is on `PATH` together with the host libstdc++ headers (e.g. `/usr/lib/gcc/x86_64-redhat-linux/15`).

## Authoring new test suites
1. Create a test source (e.g. `tests/widgets/cases.cpp`) and tag functions with
   namespaced attributes understood by the generator. You can attach a suite name to
   a namespace once and write case names relative to it:
   ```c++
   namespace [[using gentest : suite("widgets")]] widgets {

   [[using gentest : test("basic/health"), slow, linux, req("BUG-42")]]
   void widget_smoke() {
       gentest::expect_eq(create_widget().status(), widget_status::healthy);
   }

   } // namespace widgets
   ```
   The interface target `${PROJECT_NAME}` exports the right warning suppressions to
   consumers (e.g. `-Wno-unknown-attributes` on Clang/GCC, `/wd5030` on MSVC), so no
   local `#pragma` blocks are required.
2. Add a target in `tests/CMakeLists.txt` and call `gentest_attach_codegen()` to hook the generator (the generated
   source will include your `cases.cpp`, so you don't add it directly to the target sources):
   ```cmake
   add_executable(gentest_widgets_tests support/test_entry.cpp)
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

## Clang-Tidy Check (optional)

An optional clang-tidy plugin is provided to surface the same diagnostics in editors.

- Build the module (only if Clang tidy dev libraries are available):
  - Configure with `-DGENTEST_BUILD_TIDY_PLUGIN=ON` and build.
- Load the check:
  - `clang-tidy -load ./build/tools/tidy/libGentestTidyModule.so -checks=gentest-attributes <file.cpp>`
- Behavior:
  - Unknown `gentest::` attributes (including wrong arity/types) are hard errors.
  - Attributes in other namespaces are warned as “ignored (unsupported attribute namespace)”.
  - Duplicate/conflicting flags (e.g., `linux` vs `windows`) and duplicate `test`, `owner`, `category` are errors.

This shares rules with the generator, so IDE diagnostics match codegen behavior.

Running the resulting binary executes every annotated function, printing `[ PASS ]` / `[ FAIL ]` lines and returning a
non-zero exit status when any test throws `gentest::failure`. Pass `--list` to enumerate discovered cases and inspect
their metadata: the generator includes tags, requirement IDs, and skip markers that originate from the `[[using
gentest : ...]]` attribute list.

## Fixtures (member-function tests)

Member functions can be tagged as tests and run via an auto-generated fixture harness. Three lifetimes are supported:

- Ephemeral (default): each member test gets a fresh instance of the enclosing type.
- Suite lifetime: mark the class/struct with `[[using gentest: fixture(suite)]]` to reuse a single instance for all
  tests discovered in the same suite.
- Global lifetime: mark the class/struct with `[[using gentest: fixture(global)]]` to share a single instance across
  the full test run.

Optional setup/teardown hooks are available by inheriting from the provided interfaces in `gentest/fixture.h`:

```c++
struct MyFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override { /* assertions allowed here */ }
    void tearDown() override { /* assertions allowed here */ }

    [[using gentest: test("suite/member-a")]]
    void a();

    [[using gentest: test("suite/member-b")]]
    void b();
};

struct [[using gentest: fixture(suite)]] SharedFixture { /* ... */ };
```

Global fixtures behave like GoogleTest environments—construct once, reuse everywhere, and destroy at program exit:

```c++
struct [[using gentest: fixture(global)]] GlobalEnv {
    [[using gentest: test("env/prepare")]]
    void prepare();
    [[using gentest: test("env/observe")]]
    void observe();
};
```

Execution order: free (non-member) tests run first. Member tests are grouped per-fixture and run together. You can
shuffle the order of tests within each fixture group without interleaving with other groups by passing
`--shuffle-fixtures` (optionally `--seed N` for reproducibility).

### Free-Function Fixtures

Use `[[using gentest: fixtures(A, B, C, ...)]]` on a free function to have the generator default-construct fixtures and
pass them by reference to your test. No attributes are required on the fixture types; they are ordinary structs/classes.

Behavior
- Ephemeral instances: a fresh instance of each fixture is constructed for every test invocation.
- Setup/Teardown: if a fixture derives from `gentest::FixtureSetup` and/or `gentest::FixtureTearDown`, the generated
  runner automatically calls `setUp()` before the test body and `tearDown()` after it. Hooks remain optional.
- Order: `setUp()` is called in the declaration order of fixtures; `tearDown()` is called in reverse order.
- Scope: `fixtures(...)` applies to free functions only (not member tests). Stateful semantics are not supported here.
- Composition: works with templates and parameters; fixture references appear first in the function’s argument list,
  followed by any parameterized values.

Example
```c++
#include "gentest/fixture.h" // for optional setup/teardown interfaces

namespace fx {
struct A : gentest::FixtureSetup, gentest::FixtureTearDown {
    int phase = 0;
    void setUp() override { phase = 1; }
    void tearDown() override { /* validate post-conditions */ }
};
struct B { const char* msg = "ok"; };
class C { public: int v = 7; };
} // namespace fx

[[using gentest: test("fixtures/free/basic"), fixtures(A, B, C)]]
constexpr void free_basic(fx::A& a, fx::B& b, fx::C& c) {
    gentest::expect_eq(a.phase, 1, "A::setUp ran");
    gentest::expect(std::string(b.msg) == "ok", "default value");
    gentest::expect_eq(c.v, 7, "default value");
}

// With parameters too
[[using gentest: test("fixtures/free/with-params"), fixtures(A), parameters(int, 1, 2)]]
void free_with_params(fx::A& a, int v) {
    (void)a;
    gentest::expect(v == 1 || v == 2, "param axis");
}
```

## Templates

Generated files are produced strictly from templates — no emission logic is inlined in the generator beyond simple
placeholder substitutions. This keeps the output format easy to reason about and maintain.

- Main file: `tools/templates/test_impl.cpp.tpl`
  - Placeholders:
    - `{{INCLUDE_SOURCES}}`: includes of the suite’s `cases.cpp` files.
    - `{{FORWARD_DECLS}}`: optional forward declarations for free functions (member tests don’t need these).
    - `{{TRAIT_DECLS}}`: constexpr arrays for tags and requirement IDs.
    - `{{WRAPPER_IMPLS}}`: per-test wrappers with a uniform `void(void*)` signature.
    - `{{CASE_INITS}}`: initializer list that builds the `kCases` array.
    - `{{GROUP_RUNNERS}}`: per-fixture group runner functions.
    - `{{RUN_GROUPS}}`: calls to run those groups inside the entry function.
    - `{{ENTRY_FUNCTION}}`: fully qualified entry symbol (defaults to `gentest::run_all_tests`).

- Partials under `tools/templates/partials/`:
  - `wrapper_free.tpl`, `wrapper_ephemeral.tpl`, `wrapper_stateful.tpl`
  - `case_entry.tpl`
  - `group_runner_stateless.tpl`, `group_runner_stateful.tpl`
  - `array_decl_empty.tpl`, `array_decl_nonempty.tpl`
  - `forward_decl_line.tpl`, `forward_decl_ns.tpl`

All templated braces that should appear literally in C++ must be doubled (`{{` and `}}`), including initializer lists,
function bodies, and `std::random_device{{}}` calls. Placeholders use single braces (e.g. `{name}`, `{file}`).

The emitter loads these partials once and fills them exclusively via `fmt::format(fmt::runtime(template), ...)` with
named arguments. This avoids “append soup” and makes formatting changes localized to template files.

## Parameterization & Type Matrices

gentest supports generating statically typed test matrices and parameterized tests entirely via attributes. Attributes
can be split across multiple `[[...]]` blocks on the same function.

- Template matrices (Cartesian product across type lists):
  ```c++
  template <typename T, typename U>
  [[using gentest: test("templates/hello"), template(T, int, long), template(U, float, double)]]
  void hello() { gentest::expect(true, "compile"); }
  // Expands: hello<int,double>, hello<int,float>, hello<long,double>, hello<long,float>
  ```

- Multiple parameter axes (Cartesian product across value lists):
  ```c++
  [[using gentest: test("templates/pairs")]]
  [[using gentest: parameters(int, 1, 2)]]
  [[using gentest: parameters(int, 5, 6)]]
  void pairs(int a, int b) { /* 4 tests: (1,5), (1,6), (2,5), (2,6) */ }
  ```

- String-like types are auto-quoted in generated calls; both quoted and unquoted forms are accepted in attributes:
  ```c++
  [[using gentest: test("templates/strs"), parameters(std::string, "a", b)]]
  void strs(std::string s) { /* calls: ("a"), ("b") */ }
  ```

- Mixed axes and templates:
  ```c++
  template <typename T>
  [[using gentest: test("templates/bar"), template(T, int, long), parameters(std::string, x, y)]]
  void bar(std::string s) { /* 4 tests: bar<int>("x"), bar<int>("y"), bar<long>("x"), bar<long>("y") */ }
  ```

- parameters_pack: bundle multiple arguments per row to avoid Cartesian explosion:
  ```c++
  [[using gentest: test("templates/pack"), parameters_pack((int, string), (42, a), (7, "b"))]]
  void pack(int a, std::string b) { /* 2 tests: (42, "a"), (7, "b") */ }
  ```

### NTTP (Non-Type Template Parameters)

`template()` also supports non-type template parameters via the `NTTP:` prefix. NTTP values are concatenated after type
arguments inside `<...>` in the order attributes appear and can be split across multiple `[[...]]` blocks.

```c++
template <typename T, int N>
[[using gentest: test("templates/nttp"), template(T, int), template(NTTP: N, 1, 2)]]
void nttp() {
    // Example: instantiates nttp<int,1>() and nttp<int,2>()
}
```

You can mix type sets and NTTP sets across blocks; the generator forms the Cartesian product across all sets in
declaration order and uses that to materialize test wrappers and display names.

<!-- Guardrails intentionally not enforced. If you want a large matrix,
     the generator will emit all instances as requested by attributes. -->

Notes
- Supported string-like types include: string/std::string, string_view/std::string_view, char*/const char*, and wide/UTF variants
  (wstring, u8string, u16string, u32string and their corresponding char* forms). Values are quoted with the appropriate prefix.
- Char-like types (char, wchar_t, char8_t, char16_t, char32_t) are wrapped as character literals when a single character; otherwise
  the token is used verbatim (or you can provide explicit literals).

See [`AGENTS.md`](AGENTS.md) for contribution guidelines and additional workflow conventions.
