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
- `DISCOVERY_EXTRA_ARGS <arg...>`: extra arguments forwarded to discovery (`--list-tests` / `--list-death`).
- `PROPERTIES <prop...>`: CTest properties applied to each discovered test.
- `EXPECT_SUBSTRING <text>`: required substring in combined stdout/stderr for **death tests** (optional).

## Death tests

Death tests are tagged in code with the `death` tag:

```cpp
[[using gentest: test("suite/crash_on_x"), death]]
void crash_on_x();
```

Discovery runs:
- `--list-tests` (for all tests)
- `--list-death` (for death-only tests)

Death tests are registered as `death/<case>` and executed via a harness that runs:

```
--include-death --run=<case>
```

The harness:
- treats non-zero exit as success,
- fails on normal test failures (`[ FAIL ]`),
- and optionally enforces `EXPECT_SUBSTRING` if provided.

If the binary does not support `--list-death`, configuration fails with an error.

## Examples

Basic:

```cmake
gentest_discover_tests(my_tests)
```

Filtered discovery with output matching:

```cmake
gentest_discover_tests(my_tests
  TEST_FILTER "thread_pool/*"
  EXPECT_SUBSTRING "fatal path"
)
```

## Notes

- If a death test is compiled out in a configuration (e.g., wrapped in `#ifndef NDEBUG`), it will not appear in
  `--list-death`, so no CTest entry is created.
- For non-death output matching, use CTest properties (e.g., `PASS_REGULAR_EXPRESSION`) instead of `EXPECT_SUBSTRING`.
