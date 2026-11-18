# Writing tests

A gentest case is a C++ function marked with an attribute. This page shows where
cases live, how to mark and name them, and which tags and metadata they can
carry. For the checks you call inside a case, see [Assertions](assertions.md).

## A first test file

Declare cases in a header. This header is a complete test file:

```cpp
#pragma once

#include "gentest/test.h"

#include <string>

using namespace gentest::asserts;

namespace greeting {

[[gentest::test]]
inline void default_name() {
    const std::string text = "hello";
    EXPECT_EQ(text.size(), 5u);
}

[[using gentest: test("format/with_name"), tag("fast")]]
inline void with_name() {
    const std::string text = "hello " + std::string("gentest");
    EXPECT_EQ(text, "hello gentest");
}

} // namespace greeting
```

Make the header a source of a test target and attach codegen:

```cmake
add_executable(my_tests cases.hpp)
target_compile_features(my_tests PRIVATE cxx_std_20)
target_link_libraries(my_tests PRIVATE gentest::gentest_main)
gentest_attach_codegen(my_tests)
gentest_discover_tests(my_tests)
```

`gentest_attach_codegen()` runs `gentest_codegen` during the build and adds
generated registration sources to the target. `gentest::gentest_main` provides
`main()`, and `gentest_discover_tests()` registers each case with CTest. The
[README](../README.md) shows the whole project, and the install guides for
[Linux](install/linux.md), [macOS](install/macos.md) and
[Windows](install/windows.md) walk through a first build.

`./my_tests --list` shows the two cases:

```text
greeting/default_name (cases.hpp:12)
greeting/format/with_name [gentest:tags=fast] (cases.hpp:18)
```

`gentest/test.h` provides what an ordinary case needs: the checks, skip and
xfail, logging and failure context. Coroutine tests also include
`gentest/async.h` (see [Async tests](async.md)). Fixture setup and teardown
hooks and the runner API need `gentest/runner.h`, which provides everything in
both. Code in
named modules uses `import gentest;` instead.

## Where cases live

gentest uses header registration: you declare cases in headers, and codegen adds
generated registration sources (`*.header_registration.gentest.cpp`) that
include those headers and register the cases. Four rules follow from that:

- Declare each case in a header (`.h`, `.hh`, `.hpp`, `.hxx`, `.inl`, `.ipp` or
  `.tpp`) that belongs to the test target: either included by one of its source
  files, or listed in its sources or one of its header file sets.
- Define a case in a header as `inline`. The generated source and every other
  translation unit that includes the header compile its definition. Function
  templates don't need `inline`.
- Give the case external linkage: not `static` and not in an anonymous
  namespace. The generated source calls it from another translation unit.
- Don't write gentest attributes in a source file such as a `.cpp` file. The
  generated source can't see a declaration that exists only there.

Codegen stops the build when a case breaks one of these rules:

| Problem | Codegen message |
|---|---|
| A gentest attribute in a `.cpp` file | `Gentest attributes in an authored .cpp are not supported by header-declaration registration; annotate the header declaration instead` |
| A `static` case, or one in an anonymous namespace | `header-declared Gentest cases require external linkage; static and anonymous-namespace cases are not supported` |
| A non-inline case definition in a header | `a Gentest case definition in a header must be inline; put a declaration in the header and the definition in a normally compiled .cpp` |

Annotated fixture types also belong in headers; see [Fixtures](fixtures.md).
Cases in named-module interface units use module registration
(`MODULE_REGISTRATION`) instead; see [Named modules](modules.md).

### Out-of-line definitions

To keep the bodies in a `.cpp` file, put the annotated declaration in the header and the plain definition in a
`.cpp` file that the target compiles. `cases.hpp`:

```cpp
#pragma once

#include "gentest/test.h"

namespace parser {

[[using gentest: test("empty_input")]]
void empty_input();

} // namespace parser
```

`cases.cpp`:

```cpp
#include "cases.hpp"

#include <string>

using namespace gentest::asserts;

namespace parser {

void empty_input() {
    const std::string input;
    EXPECT_TRUE(input.empty());
}

} // namespace parser
```

List both files in the target:

```cmake
add_executable(my_tests cases.cpp cases.hpp)
```

The fixture and parameter types the case uses must be visible in the header,
because the generated source includes only the header. Don't repeat the
attribute on the definition. [examples/hello](../examples/hello) uses this layout, and
[examples/hello_header_only](../examples/hello_header_only) keeps everything in
the header.

## Attribute spelling

gentest attributes are standard C++ attributes in the `gentest` namespace. Write
a single attribute as `[[gentest::...]]` and a list as `[[using gentest: ...]]`:

```cpp
[[gentest::test("math/add")]]
inline void add() {}

[[using gentest: test("math/subtract"), tag("slow"), req("BUG-123")]]
inline void subtract() {}
```

Codegen rejects a gentest attribute written without the namespace, such as
`[[test]]`:

```text
attribute 'test' must use '[[using gentest: ...]]' or explicit 'gentest::' qualification
```

Compilers ignore attributes in the `gentest` namespace. Linking
`gentest::gentest` (directly or through `gentest::gentest_main`) adds the flags
that silence their unknown-attribute warnings: `-Wno-unknown-attributes` and
`-Wno-attributes` for Clang, `-Wno-attributes` for GCC and `/wd5030` for MSVC.

## Names

A case's full name is its namespace path and its base name, joined by `/`:

- The base name is the argument of `test("...")`. Without an argument, it is
  the function name.
- The namespace path is the enclosing named namespaces, joined by `/`.

```cpp
namespace net::http {

[[gentest::test]]
inline void parses_status() {} // net/http/parses_status

[[gentest::test("headers/case_insensitive")]]
inline void header_lookup() {} // net/http/headers/case_insensitive

} // namespace net::http
```

You select cases by these names (see [Test selection](test_selection.md)), and
CTest uses them as test names. Pass a name to
`test("...")` to keep it stable when you rename the function, or to tell
overloads apart. Names must be unique in a test target; codegen reports a clash
as `duplicate test name '<name>' at <file>:<line> (previously declared at <file>:<line>)`.

Template and parameterized cases add their template arguments and parameter
values to the name, as in `sort<int>` or `bounded_value(-1)`. See
[Parameters and templates](parameters.md).

### Suites

The namespace path is also the case's suite. Suites group the cases of a run,
and suite fixtures belong to them; see [Fixtures](fixtures.md).

Codegen rejects gentest attributes on a namespace that holds cases or shared
fixtures, so the suite is always the namespace path. To group cases another
way:

- Declare them in the namespace whose path you want as their suite.
- Name them with `test("...")`. The suite stays the namespace path, but a `/`
  in the name groups cases below it: in the example above,
  `--filter='net/http/headers/*'` selects the cases whose `test("...")` names
  start with `headers/`. See [Test selection](test_selection.md).
- Build them into separate test executables, which run and report separately.

## Tags and metadata

Tags and metadata are attributes on the case declaration:

```cpp
#pragma once

#include "gentest/test.h"

namespace net::io {

[[using gentest: tag("slow"), owner("team-io"), req("REQ-1")]]
inline void read() {}

} // namespace net::io
```

Use `tag("integration", "gpu")` for labels chosen by your project. Tags are
available before execution for selection and CTest discovery. Each argument
must be a nonempty ordinary or raw string literal without NUL characters.
Ordinary literals support C++20 escape sequences; raw literals preserve their text.
You may repeat `tag(...)`; duplicate labels are combined ignoring ASCII case,
retaining the first spelling. Labels such as
`linux` and `windows` can coexist and have no platform or execution behavior.

| Attribute | Meaning |
|---|---|
| `tag("integration", ...)` | User-defined labels for selection and reporting. |
| `owner("team-runtime")` | Declared owner metadata. Takes one string and appears once per case; it does not add a tag. |
| `req("BUG-123", ...)` | Requirement IDs for traceability. `requires(...)` is an alias. See [Traceability](traceability_standards.md). |
| `death` | Marks a death test, excluded unless `--include-death` is passed. See [Death tests](death_tests.md). |
| `skip("reason")` | Reports the case as skipped without executing it. The reason is optional. See [Assertions](assertions.md). |

A label has no execution semantics: `tag("death")` is an ordinary label, while
`death` is a behavioral attribute. Unknown attribute names are errors; arbitrary
labels belong inside `tag(...)`.

Use [runtime properties](recording.md) for facts learned during execution,
such as a random seed or device name. Those properties are unavailable during
selection or discovery, and do not change declared metadata.

The metadata shows up here:

- `--list` prints it after the name. For the example above:

  ```text
  net/io/read [gentest:tags=slow;requires=REQ-1;owner=team-io] (cases.hpp:8)
  ```

- `--list-tags` prints every tag in the test target, in lowercase.
- `--tag` and `--exclude-tag` select cases by tag, ignoring ASCII case. See
  [Test selection](test_selection.md).
- `--list-json` writes `tags`, `requirements`, `owner`, `is_death`, `skipped` and
  `skipReason` for each case. See [Test inventory](test_inventory.md).
- JUnit reports write each requirement as
  `<property name="requirement" value="..."/>`, and Allure reports add `tag` and
  `requirement` labels. See [Reports](reports.md).
- `gentest_discover_tests()` adds each tag as a CTest label, so
  `ctest -L slow` runs the slow cases. See [CTest integration](discover_tests.md).

[examples/metadata](../examples/metadata) exports this metadata from a small
test target.

## Attribute reference

These are all the attributes codegen accepts. Any of the function attributes
marks the function as a case, so `[[gentest::tag("slow")]]` alone declares a test named
after the function.

On functions:

| Attribute | Arguments | Meaning |
|---|---|---|
| `test` | none, or one name | A test case. |
| `bench` (alias `benchmark`) | one name | A benchmark. See [Benchmarks](benchmarks.md). |
| `jitter` | one name | A jitter measurement. See [Benchmarks](benchmarks.md). |
| `baseline` | none | The measured case that reports compare the other cases in its suite against. |
| `items_per_call` (alias `ops_per_call`) | a positive integer | Logical items one timed call processes. |
| `bench_once` | none | Runs a benchmark exactly once, without calibration or warmup. |
| `bench_warmup` | an integer, 0 or more | Warmup epochs for this benchmark. |
| `bench_time` | `"runner"` or `"supplied"` | Who times the benchmark: the runner (the default), or your code (needs `bench_once`). |
| `parameters` | a parameter name, then values | One case per value. See [Parameters](parameters.md). |
| `parameters_pack` | a tuple of names, then tuples of values | One case per row of values. |
| `range` | name, start, step, end; or name, `"start:step:end"` | One case per value from start to end. |
| `linspace` | name, start, end, count | `count` evenly spaced values. |
| `geom` (aliases `geomspace`, `geospace`) | name, start, factor, count | A geometric sequence of `count` values. |
| `logspace` | name, start exponent, end exponent, count, optional base | `count` values spaced evenly on a log scale (base 10 by default). |
| `template` | a template parameter name, then arguments | One case per template argument. |
| `tag`, `death`, `owner`, `req` (alias `requires`), `skip` | see [Tags and metadata](#tags-and-metadata) | Tags, death-test behavior, owner, requirements and static skip. |

`baseline` and `items_per_call` need `bench` or `jitter` on the same
declaration. `bench_once`, `bench_warmup` and `bench_time` need `bench` and
can't be combined with `test` or `jitter`. [Benchmarks](benchmarks.md) explains
them.

On classes, `fixture(suite)` and `fixture(global)` declare a shared fixture; see
[Fixtures](fixtures.md). Namespaces take no gentest attributes: a namespace's
path is the suite of the cases in it; see [Suites](#suites).

## Member tests

Member tests, annotated member functions of a class, are deprecated (see
[DEPRECATIONS.md](../DEPRECATIONS.md)). Write a free function that takes the
fixture as a parameter instead. A member test's base name is
`FixtureType/method`; [Fixtures](fixtures.md) describes how it gets its
fixture.

## Rules and edge cases

- `test` takes no argument or one non-empty string.
- Write the attributes right before the declaration. Codegen reads them from
  the source text: a comment in between is fine, but a preprocessor line such
  as `#endif` between the attributes and the declaration hides them, and the
  function is not registered.
- You can annotate several declarations of one case, for example a declaration
  and the inline definition in the same header, if the attributes agree.
  Otherwise codegen reports `annotated redeclarations of one entity differ`.
- If two translation units see an annotated header differently, for example
  because of different macro definitions, codegen reports
  `header declaration differs between compile contexts`.
- Codegen ignores annotations in system headers, such as headers found through
  an `-isystem` include path.
- `gentest_attach_codegen()` rejects generator expressions in the target's
  sources; list concrete files. It also turns off unity builds and C++ module
  scanning for a header-registration target.

## Related pages

- [Assertions](assertions.md): checks, exceptions, skip and xfail.
- [Failure diagnostics](failure_diagnostics.md): failure context, matchers and
  mock failure locations.
- [Parameters](parameters.md), [Fixtures](fixtures.md),
  [Death tests](death_tests.md), [Async tests](async.md),
  [Benchmarks](benchmarks.md) and [Mocks](mock_generation.md) for the other
  kinds of cases.
- [Logging and threads](logging_and_threads.md): `gentest::log()` and checks in
  multi-threaded tests.
