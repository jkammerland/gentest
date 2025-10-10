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
  - Duplicate/conflicting flags (e.g., `linux` vs `windows`) and duplicate `test`, `owner`, `group` are errors.

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

## Mocks

`gentest::mock<T>` synthesizes strongly-typed mocks directly from your test source. Include `gentest/mock.h`, instantiate
the mock, and configure expectations via `gentest::expect(mock, &T::method)`:

```c++
#include "gentest/mock.h"

struct Clock {
    virtual ~Clock() = default;
    virtual int  now() const = 0;
    virtual void reset(int value) = 0;
};

[[using gentest: test("mock/clock/basic")]]
void mock_clock_basic() {
    gentest::mock<Clock> clock;

    gentest::expect(clock, &Clock::now).times(1).returns(1234);
    gentest::expect(clock, &Clock::reset).times(2).invokes([](int value) {
        EXPECT_EQ(value % 5, 0);
    });

    Clock *iface = &clock;
    EXPECT_EQ(iface->now(), 1234);
    iface->reset(10);
    iface->reset(15);
}
```

- Expectations support `.times(n)`, `.returns(value)` (non-void), `.invokes(callable)`, `.with(args...)`, and `.allow_more()`.
- `.with(args...)` enables simple positional argument matching via `==` for each parameter. On mismatch, a failure is
  recorded in the active test context with a detailed message (argument index, method name, expected vs. actual). Calls
  still count against the current expectation in FIFO order.
- Missing calls or unexpected invocations are surfaced through the active test context (identical to other assertions).
- Polymorphic targets produce `mock<T> : T` overrides; non-virtual classes receive standalone mocks that mirror the
  public surface so they remain drop-in replacements for templated injection and CRTP patterns.
- The generator emits `mock_registry.hpp` and an inline `mock_impl.hpp` alongside each suite’s `test_impl.cpp`. The
  build defines `GENTEST_MOCK_REGISTRY_PATH` so `gentest/mock.h` automatically includes the registry when compiling your
  tests; the generated `test_impl.cpp` includes `gentest/mock.h` after including your test sources so the generated mock
  registry and inline implementations are visible once all original types are in scope. No extra translation unit is used
  for mocks.

Using mocks in helpers/outside tests
- You can freely use `gentest::mock<T>` in helper headers or other files that are part of the same test target, even
  outside `[[using gentest: test]]` functions. The CMake helper defines:
  - `GENTEST_MOCK_REGISTRY_PATH` so including `gentest/mock.h` brings in the generated registry specializations.
  - `GENTEST_MOCK_IMPL_PATH` so inline method definitions are visible to any TU in the target.
  - Additionally, the generated test TU itself includes `gentest/mock.h` after your sources, ensuring type completeness
    for virtual (polymorphic) mocks that derive from their targets.
- Discovery still requires that at least one scanned source (or a header included from it) contains `gentest::mock<T>`
  instantiations or references, so the generator knows which mocks to produce. Add such uses to your `SOURCES` passed to
  `gentest_attach_codegen()` or include a header that references `mock<T>`.
- Avoid calling `gentest::expect(...)` outside an active test context; configure expectations inside test bodies or
  fixture setup hooks. Using the mock type itself (constructing, taking member pointers) is fine in helper code.

### Matchers

In addition to positional equality via `.with(...)`, you can use per-argument matchers with `.where_args(...)` or the
alias `.where(...)`. Matchers are lightweight objects that validate an argument and can describe mismatches clearly.

- Basic
  - `Any()` – accepts any value
  - `Eq(x)` – `== x`
  - `InRange(lo, hi)` – inclusive range `[lo, hi]`
  - `Not(m)` – negates another matcher
- Comparators
  - `Ge(x)`, `Le(x)`, `Gt(x)`, `Lt(x)`
  - `Near(x, eps)` – floating point near comparison
- Strings (argument convertible to `std::string_view`)
  - `StrContains("needle")`, `StartsWith("pre")`, `EndsWith("suf")`
- Composition
  - `AnyOf(m1, m2, ...)`, `AllOf(m1, m2, ...)`
- Whole-call predicate
  - `where_call([](const Args&...) { ... })` for cross-argument checks

Examples:

```c++
using namespace gentest::match;
gentest::mock<Calculator> mock_calc;
gentest::expect(mock_calc, &Calculator::compute)
    .times(1)
    .where(Eq(12), Any())
    .returns(300);

gentest::mock<Ticker> mock_tick;
gentest::expect(mock_tick, &Ticker::tick)
    .times(2)
    .where_args(InRange(5, 10))
    .invokes([&](int) { /* ... */ });

// Whole-call
gentest::expect(mock_calc, &Calculator::compute)
    .where_call([](int lhs, int rhs) { return (lhs + rhs) % 2 == 0; })
    .returns(42);
```

## Reporting

The generated runner can produce machine-readable reports and CI annotations in addition to the standard console output.

- JUnit XML
  - Write a minimal, CI-friendly JUnit report with `--junit=<file>`.
  - Grouped by suite; each test includes its status and time in seconds; failure messages are wrapped in CDATA.
  - Example:
    ```bash
    ./build/debug/tests/gentest_unit_tests --junit ./build/junit-unit.xml
    ```

- GitHub Annotations
  - Emit `::error file=...,line=...,title=...::message` lines on failures for GitHub Actions log surfaces.
  - Enable via `--github-annotations` or by setting `GITHUB_ACTIONS=1` in the environment.
  - Example output snippet:
    ```
    ::error file=tests/unit/cases.cpp,line=42,title=unit/arithmetic/sum::EXPECT_EQ failed at ...
    ```

- Allure Results
  - Generate Allure 2 result JSON files with `--allure-dir=<dir>` (directory created if missing).
  - Each test produces a `result-*.json` with status (passed/failed/skipped), duration, and a suite label; failures include message + trace content.
  - Example:
    ```bash
    ./build/debug/tests/gentest_unit_tests --allure-dir ./build/allure-results
    # then in CI:
    allure generate ./build/allure-results -o ./build/allure-report
    ```

Color output can be disabled with `--no-color`, or via the `NO_COLOR` / `GENTEST_NO_COLOR` environment variables.

### CI Artifacts (GitHub Actions)

Use upload-artifact to collect JUnit and Allure results:

```yaml
name: tests
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake --preset=debug
      - name: Build
        run: cmake --build --preset=debug -j
      - name: Run tests (JUnit + Allure)
        run: |
          mkdir -p build/debug/tests/artifacts
          ctest --preset=debug --output-on-failure || true
          # Example: run unit with JUnit and Allure artifacts
          build/debug/tests/gentest_unit_tests \
            --junit build/debug/tests/artifacts/junit-unit.xml \
            --allure-dir build/debug/tests/artifacts/allure-unit \
            --github-annotations || true
      - name: Upload JUnit
        uses: actions/upload-artifact@v4
        with:
          name: junit-xml
          path: build/debug/tests/artifacts/*.xml
      - name: Upload Allure
        uses: actions/upload-artifact@v4
        with:
          name: allure-results
          path: build/debug/tests/artifacts/allure-unit
```

### Optional Dependencies (Boost)

Two optional CMake toggles can enhance the generated runner without introducing mandatory dependencies:

- `GENTEST_USE_BOOST_JSON` (default OFF)
  - Uses Boost.JSON to build Allure result objects instead of the minimal manual JSON writer.
  - Header‑only in typical setups; no explicit link libraries are required.

- `GENTEST_USE_BOOST_UUID` (default OFF)
  - Uses Boost.UUID to generate RFC4122 v4 UUIDs for Allure results (otherwise a small built‑in generator is used).
  - Also header‑only in typical setups.

Enable them at configure time (applies to all generated test targets via `gentest_attach_codegen`):

```bash
cmake --preset=debug \
  -DGENTEST_USE_BOOST_JSON=ON \
  -DGENTEST_USE_BOOST_UUID=ON
cmake --build --preset=debug
```

## Templates

Generated files are produced strictly from templates — no emission logic is inlined in the generator beyond simple
placeholder substitutions. This keeps the output format easy to reason about and maintain.

- Main file: `tools/templates/test_impl.cpp.tpl`
  - Placeholders:
    - `{{INCLUDE_SOURCES}}`: includes of the suite’s `cases.cpp` files.
    - `{{INCLUDE_MOCK_IMPL}}`: include of the per-target inline mock implementation header.
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
  [[using gentest: parameters(a, 1, 2)]]
  [[using gentest: parameters(b, 5, 6)]]
  void pairs(int a, int b) { /* 4 tests: (1,5), (1,6), (2,5), (2,6) */ }
  ```

- String-like types are auto-quoted in generated calls; both quoted and unquoted forms are accepted in attributes:
  ```c++
  [[using gentest: test("templates/strs"), parameters(s, "a", b)]]
  void strs(std::string s) { /* calls: ("a"), ("b") */ }
  ```

- Mixed axes and templates:
  ```c++
  template <typename T>
  [[using gentest: test("templates/bar"), template(T, int, long), parameters(s, x, y)]]
  void bar(std::string s) { /* 4 tests: bar<int>("x"), bar<int>("y"), bar<long>("x"), bar<long>("y") */ }
  ```

- parameters_pack: bundle multiple arguments per row to avoid Cartesian explosion:
  ```c++
  [[using gentest: test("templates/pack"), parameters_pack((a, b), (42, a), (7, "b"))]]
  void pack(int a, std::string b) { /* 2 tests: (42, "a"), (7, "b") */ }
  ```

### Template Parameters (Types and Non-Types)

`template(NAME, ...)` applies to both type and value template parameters. The generator uses the function’s template
declaration to resolve `NAME` to its kind and expands the Cartesian product across all template sets in
declaration order.

Examples:

```c++
// Type + value parameter
template <typename T, int N>
[[using gentest: test("templates/nttp"), template(T, int), template(N, 1, 2)]]
void nttp() { /* instantiates: <int,1>, <int,2> */ }

// Interleaved order
template <int N, typename T>
[[using gentest: test("templates/interleaved"), template(N, 1, 2), template(T, int, long)]]
void interleaved() { /* 4 instances: N in {1,2} × T in {int,long} */ }

// Mixed with runtime value parameters
  template <typename T, std::size_t N>
  [[using gentest: test("templates/mix/type_nttp_value"), template(T, int), template(N, 16), parameters(v, 3)]]
  void mix_type_nttp_value(int v) { /* 1 instance: <int,16>(3) */ }
  ```

Notes
- Values given for value template parameters are used verbatim; the C++ compiler ensures they match the declared
  parameter type (e.g., `bool`, `int`, `std::size_t`).
- You can mix and split `template(...)` attributes across multiple `[[...]]` blocks; order is determined by the template
  parameter list as declared in the function signature.

<!-- Guardrails intentionally not enforced. If you want a large matrix,
     the generator will emit all instances as requested by attributes. -->

Additional Notes
- Supported string-like types include: string/std::string, string_view/std::string_view, char*/const char*, and wide/UTF variants
  (wstring, u8string, u16string, u32string and their corresponding char* forms). Values are quoted with the appropriate prefix.
- Char-like types (char, wchar_t, char8_t, char16_t, char32_t) are wrapped as character literals when a single character; otherwise
  the token is used verbatim (or you can provide explicit literals).

### Named Parameters

`parameters(name, v1, v2, ...)` takes the declared function parameter name and a list of expressions. The type is inferred from
the function signature, so you don’t repeat it. Values are parsed as full expressions; commas inside braces/parentheses are handled.

```c++
[[using gentest: test("params")]]
[[using gentest: parameters(i, 0, 10, 100)]]
void params_test(int i);

// Struct expressions are supported directly
[[using gentest: test("structs")]]
[[using gentest: parameters(p, Point{1,2}, Point{3,4})]]
void takes_point(Point p);
```

For multiple parameters per row, use `parameters_pack((n1, n2, ...), (v1, v2, ...), ...)` with names instead of types:

```c++
[[using gentest: test("pack")]]
[[using gentest: parameters_pack((a, b), (42, s1), (7, "b"))]]
  void pack(int a, std::string b);
  ```

Rules and guarantees
- Names must match declared function parameters (unknown/duplicate names are hard errors).
- All parameters of the function must be supplied via `parameters(...)` and/or `parameters_pack(...)` when provided;
  otherwise codegen reports a clear error.
- Values for string-like parameters are auto-quoted based on the parameter type; user-defined types are passed as-is.

Multi-block attributes
- Attributes compose across multiple `[[...]]` blocks on the same declaration. Splitting parameters/packs/templates is allowed:

```c++
[[using gentest: test("multi_blocks/params_split")]]
[[using gentest: parameters(a, 1, 2)]]
[[using gentest: parameters(b, 10)]]
void multi_params_split(int a, int b);

[[using gentest: test("multi_blocks/pack_split")]]
[[using gentest: parameters_pack((a, b), (1, 2), (3, 4))]]
[[using gentest: parameters_pack((c), (5))]]
void multi_pack_split(int a, int b, int c);

template <typename T, int N>
[[using gentest: test("multi_blocks/mixed_split")]]
[[using gentest: template(T, int)]]
[[using gentest: template(N, 7)]]
[[using gentest: parameters(s, Hello, "World")]]
void multi_mixed_split(std::string s);
```

Struct parameters (defined in the test TU)
- You can pass user-defined types directly using named parameters. Types declared in the test source are visible to the generated TU:

```c++
struct LocalPoint { int x; int y; };

[[using gentest: test("local_struct/axis"), parameters(p, LocalPoint{1,2}, LocalPoint{3,4})]]
void local_struct_axis(LocalPoint p);

[[using gentest: test("local_struct/pack"), parameters_pack((p, q), (LocalPoint{1,2}, LocalPoint{3,4}), (LocalPoint{5,6}, LocalPoint{7,8}))]]
void local_struct_pack(LocalPoint p, LocalPoint q);
```

### Naming & CLI

Every test has a final, user-facing name used by the CLI for listing and selection.

- Base name (test): optional
  - If `test("name")` is present, it is the base name.
  - If omitted, the base name falls back to the C++ function name.

- Suite path (namespace): derived or overridden
  - By default, gentest derives the suite path from the function’s fully qualified C++ namespace, joining components with `/`.
    Anonymous namespaces are ignored. Examples:
      - `namespace n1::n2 { void f(); }` → suite path `n1/n2`
      - Global namespace → no suite prefix
  - To override the default, annotate an enclosing namespace with `[[using gentest: suite("alpha/beta")]]`. The nearest override wins; its string
    is used verbatim as the suite path for all tests contained within.

- Final name and instances
  - Final name = `suite_path + "/" + base_name` (or just `base_name` if no suite).
  - Template instances append `"<...>"`; value parameter instances append `"(...)"`.

- Uniqueness
  - gentest enforces that `suite_path/base_name` (before decorations) is unique across the entire test binary, even when multiple
    source files/TUs contribute tests to the same namespace. Duplicate names produce a clear generator error with both file:line
    locations; disambiguate by passing `test("...")` or renaming the function.

- CLI examples
  - List exact names: `--list-tests`
  - Run exact: `--run-test=n1/n2/my_case<int>(42)`
  - Filter: `--filter=n1/*/my_case*`

See [`AGENTS.md`](AGENTS.md) for contribution guidelines and additional workflow conventions.
