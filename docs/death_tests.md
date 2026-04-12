# Death tests

This doc explains how gentest marks, discovers, and runs death tests. The short version is:
- Tag tests with the `death` tag in code.
- The runner excludes death tests by default to avoid aborting the entire run.
- `gentest_discover_tests()` automatically registers death tests as separate CTest entries.

## Tagging in code

```cpp
[[using gentest: test("death/fatal_path"), death]]
void fatal_path();
```

`death` is the only supported death tag.

## Runner behavior

- Default runs exclude death-tagged tests.
- Run them explicitly with:

```bash
./my_tests --include-death --run=death/fatal_path
```

- List only death tests:

```bash
./my_tests --list-death
```

## CTest discovery harness

`gentest_discover_tests()` always registers death tests separately and runs them via the
`GentestCheckDeath.cmake` harness so they execute in their own process.

- Death tests are registered with the `death/` prefix by default. If your test names already include `death/`,
  you'll see entries like `death/death/fatal_path` in CTest.
- Discovery uses `--list` and infers death-tagged cases from the emitted gentest metadata (`tags=death`).
- The harness runs `--include-death --run=<case>` and expects a non-zero exit code.
- If the test binary reports a normal test failure (`[ FAIL ]`), the harness marks it as a failure.
- If the test binary reports "Case not found:" (or legacy "Test not found:"), the harness marks the CTest
  entry as skipped.
- Full discovery options live in `docs/discover_tests.md`.

### Output expectations

`gentest_discover_tests()` supports a `DEATH_EXPECT_SUBSTRING` option:

```cmake
gentest_discover_tests(my_tests
  DEATH_EXPECT_SUBSTRING "fatal path"
)
```

For death tests, the harness requires this substring in combined stdout/stderr.
The option is scoped to one `gentest_discover_tests(...)` call, not to one individual case.
If that call discovers several death tests, they all use the same expected substring.

Use `TEST_FILTER` to scope the expectation:

```cmake
gentest_discover_tests(my_tests
  TEST_FILTER "death/network/*"
  DEATH_EXPECT_SUBSTRING "network fatal"
)
```

For a single death test, use an exact filter:

```cmake
gentest_discover_tests(my_tests
  TEST_FILTER "death/fatal_path"
  DEATH_EXPECT_SUBSTRING "fatal path"
)
```

If different death tests need different expected output, register them through separate non-overlapping
`gentest_discover_tests(...)` calls so each death case is discovered exactly once.

For non-death tests, use CTest properties (e.g., `PASS_REGULAR_EXPRESSION`) if you need output matching.

### Compiled-out death tests

If a death test is compiled out in a given configuration (e.g., wrapped in `#ifndef NDEBUG`), it will not appear
in `--list`, so no CTest entry is created for that configuration.

## Troubleshooting

- If you see clang "unknown argument" warnings for module flags during discovery, you are likely using a compiler
  without C++ module support; disable module scanning via `CMAKE_CXX_SCAN_FOR_MODULES=OFF` or upgrade clang.
