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
```bash
cmake --preset=debug
cmake --build --preset=debug
ctest --preset=debug --output-on-failure
```

`gentest_codegen` relies on clang libraries and the active build’s `compile_commands.json`. vcpkg users can install the
`llvm[clang,clang-tools-extra]` port plus `fmt` (pulled automatically for tests), and everyone else should ensure
`clang++` is on `PATH` together with the host libstdc++ headers (e.g. `/usr/lib/gcc/x86_64-redhat-linux/15`).

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

Member functions can be tagged as tests and run via an auto-generated fixture harness. Two modes are supported:

- Stateless (default): each member test gets a fresh instance of the enclosing type.
- Stateful: mark the class/struct with `[[using gentest: stateful_fixture]]` to reuse a single instance for all its
  member tests.

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

struct [[using gentest: stateful_fixture]] StatefulFixture { /* ... */ };
```

Execution order: free (non-member) tests run first. Member tests are grouped per-fixture and run together. You can
shuffle the order of tests within each fixture group without interleaving with other groups by passing
`--shuffle-fixtures` (optionally `--seed N` for reproducibility).

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
