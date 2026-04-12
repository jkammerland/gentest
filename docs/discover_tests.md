# Test Discovery (`gentest_discover_tests`)

This doc explains the CMake helper that wires gentest binaries into CTest.

## Summary

`gentest_discover_tests(<target> ...)` runs the test executable to discover cases and creates one CTest entry per case.
It also auto-registers death tests (tagged `death`) via a dedicated harness so they run in their own process.

## Required vs optional arguments

Required:
- `target` (first positional argument): the CMake executable target to discover tests from.

Optional:
- `TEST_PREFIX <prefix>`: prepends this string to each discovered test name.
- `TEST_SUFFIX <suffix>`: appends this string to each discovered test name.
- `TEST_FILTER <pattern>`: wildcard filter applied to discovered case names (supports `*` and `?`).
- `WORKING_DIRECTORY <dir>`: working directory used when invoking the test binary (default: `CMAKE_CURRENT_BINARY_DIR`).
- `TEST_LIST <var>`: CMake variable that receives the list of discovered tests.
- `DISCOVERY_TIMEOUT <seconds>`: timeout for discovery calls (default: `5`).
- `DISCOVERY_MODE <POST_BUILD|PRE_TEST>`:
  - `POST_BUILD`: run discovery at build time and write a CTest include file.
  - `PRE_TEST`: run discovery right before `ctest` executes (slower but always up-to-date).
- `EXTRA_ARGS <arg...>`: extra arguments forwarded to each CTest test invocation.
- `DISCOVERY_EXTRA_ARGS <arg...>`: extra arguments forwarded to discovery (`--list`).
- `PROPERTIES <prop...>`: CTest properties applied to each discovered test.
- `DEATH_EXPECT_SUBSTRING <text>`: required substring in combined stdout/stderr for **death tests** (optional).

## Death tests

Death tests are tagged in code with the `death` tag:

```cpp
[[using gentest: test("suite/crash_on_x"), death]]
void crash_on_x();
```

Discovery runs:
- `--list`

Death-tagged cases are inferred from the metadata emitted by `--list` output (specifically `tags=death`).

Death tests are registered as `death/<case>` and executed via a harness that runs:

```
--include-death --run=<case>
```

The harness:
- treats non-zero exit as success,
- fails on normal test failures (`[ FAIL ]`),
- and optionally enforces `DEATH_EXPECT_SUBSTRING` if provided.

`DEATH_EXPECT_SUBSTRING` is scoped to a single `gentest_discover_tests(...)` call.
If that call discovers multiple death tests, the same substring is required for all of them.
There is no built-in per-test map such as `case -> expected substring`.

To use different death-output expectations, split discovery into multiple non-overlapping calls with `TEST_FILTER`.
Make sure each death test is registered by exactly one call.

## Examples

Basic:

```cmake
gentest_discover_tests(my_tests)
```

Filtered discovery with death-output matching:

```cmake
gentest_discover_tests(my_tests
  TEST_FILTER "death/*"
  DEATH_EXPECT_SUBSTRING "fatal path"
)
```

Apply one expectation to a group of death tests:

```cmake
gentest_discover_tests(my_tests
  TEST_FILTER "death/network/*"
  DEATH_EXPECT_SUBSTRING "network fatal"
)
```

Apply one expectation to a single death test:

```cmake
gentest_discover_tests(my_tests
  TEST_FILTER "death/fatal_path"
  DEATH_EXPECT_SUBSTRING "fatal path"
)
```

If you need multiple different death expectations for the same target, partition discovery:

```cmake
gentest_discover_tests(my_tests
  TEST_FILTER "unit/*")

gentest_discover_tests(my_tests
  TEST_FILTER "death/network/*"
  DEATH_EXPECT_SUBSTRING "network fatal")

gentest_discover_tests(my_tests
  TEST_FILTER "death/io/*"
  DEATH_EXPECT_SUBSTRING "io fatal")
```

Avoid a broad unfiltered `gentest_discover_tests(my_tests)` call plus narrower death-only calls for the same target,
because that would register the overlapping death tests more than once.

## Notes

- If a death test is compiled out in a configuration (e.g., wrapped in `#ifndef NDEBUG`), it will not appear in
  `--list`, so no CTest entry is created.
- For non-death output matching, use CTest properties (e.g., `PASS_REGULAR_EXPRESSION`) instead of `DEATH_EXPECT_SUBSTRING`.
