# Death tests

A death test checks that code ends the process: it aborts, crashes or exits
with a non-zero status. gentest runs each death test in its own process, so the
process that ends isn't your whole test run.

Mark the case with the `death` attribute. A plain run of the test executable leaves
death tests out; `gentest_discover_tests()` registers each one as its own CTest
entry, which runs the case in a separate process and checks that it died.

## Write a death test

```cpp
#pragma once

#include "gentest/test.h"

#include <cstdio>
#include <cstdlib>

namespace crash {

// Code under test: stops the program when its input is invalid.
inline void check_positive(int value) {
    if (value <= 0) {
        std::fputs("fatal path: value must be positive\n", stderr);
        std::abort();
    }
}

[[using gentest: test, death]]
inline void fatal_path() {
    check_positive(0);
}

} // namespace crash
```

The case is named `crash/fatal_path`. The `death` attribute makes it a death test. A user-defined
`tag("death")` has no effect on execution.

A death test passes when the process ends with a non-zero exit status or a
fatal signal after the test body has started. It fails when the body returns
normally, or when the process ends before the body starts, for example in
fixture setup.

With exceptions enabled, a failed `ASSERT_*` or `gentest::require*` check
doesn't end the process. It throws `gentest::assertion`, which fails only the
current case, and the runner carries on. A death test that relies on such a
check fails with
`Death test exited non-zero but reported a normal test failure.` See
[Assertions](assertions.md) for how checks fail, and
[Builds without exceptions](#builds-without-exceptions) for the builds where
they do end the process.

## Run death tests with CTest

```cmake
gentest_discover_tests(my_tests)
```

Each death test becomes its own CTest entry named
`death/<case>`, here `death/crash/fatal_path`. `TEST_PREFIX` goes before
`death/`, and `TEST_SUFFIX` after the case name. A case whose name already
starts with `death/` shows up as `death/death/...`, so give death tests another
name prefix, such as `crash/`.

Each user-defined tag becomes a CTest label. To select death tests by label,
explicitly add a label such as `tag("fatal-path")`, then use `ctest -L fatal-path`.
Without extra labels, use the CTest name prefix: `ctest -R '^death/'`.

A death test with a [static skip](assertions.md) gets no CTest entry. The other
arguments of `gentest_discover_tests()` are described in
[CTest integration](discover_tests.md).

## Check what a death test prints

`DEATH_EXPECT_SUBSTRING` makes each death test of a `gentest_discover_tests()`
call check its output:

```cmake
gentest_discover_tests(my_tests DEATH_EXPECT_SUBSTRING "fatal path")
```

The text must appear in the combined stdout and stderr of the death test. It
applies to every death test that the call registers; there is no per-case
setting.

To give groups of death tests different expectations, give each group a common
name prefix and register each group with its own call. `TEST_FILTER` matches
case names, not the `death/` prefix of CTest names, so `TEST_FILTER "death/*"`
selects the cases whose own names start with `death/`, not the death tests.

```cmake
gentest_discover_tests(my_tests
  TEST_FILTER "unit/*")

gentest_discover_tests(my_tests
  TEST_FILTER "crash/network/*"
  DEATH_EXPECT_SUBSTRING "network fatal")

gentest_discover_tests(my_tests
  TEST_FILTER "crash/io/*"
  DEATH_EXPECT_SUBSTRING "io fatal")
```

For one death test, use its full name as the filter, for example
`TEST_FILTER "crash/fatal_path"`.

Make the filters disjoint and cover every case: a case that two calls match is
registered twice, and a case that no call matches isn't registered. Don't add
an unfiltered `gentest_discover_tests(my_tests)` next to narrower calls for the
same target.

`DEATH_EXPECT_SUBSTRING` has no effect on ordinary cases. To check their
output, set a CTest property such as `PASS_REGULAR_EXPRESSION` on their
entries.

## How a death test is judged

The CTest entry runs a CMake script, `GentestCheckDeath.cmake`, which starts
the executable like this, where `<EXTRA_ARGS>` are the `EXTRA_ARGS` of the
`gentest_discover_tests()` call:

```text
my_tests --include-death --run=<case> <EXTRA_ARGS> --gentest-death-marker=<file> --skip-return-code=77
```

The runner writes the case name to the marker file after fixture setup, just
before calling the test body. That is how the script tells a death in the body
from one in setup:

| What happened | CTest result | Message |
|---|---|---|
| The body started, then the process exited non-zero or died from a signal, and printed the expected text if one is set. | Passed | `Death test passed (non-zero exit and expected output present)` |
| The process exited with status 0, for example because the body returned. | Failed | `Expected process to abort/exit non-zero, but exit code was 0.` |
| The output has a `[ FAIL ]` line: the case failed normally instead of ending the process. | Failed | `Death test exited non-zero but reported a normal test failure.` |
| The body finished without ending the process, and the process failed afterwards, for example in teardown. | Failed | `Death test body completed without the expected termination.` |
| The process ended before the body started, for example in fixture setup or on a command-line error. | Failed | `Death test did not reach the selected test body.` |
| The expected text is missing from the output. | Failed | `Expected substring not found in output: '<text>'.` |
| The executable couldn't be started. | Failed | `Death test process could not execute: <reason>` |
| The case skipped at run time, for example with `gentest::skip()`. | Skipped | `[ SKIP ] Death test skipped by test binary` |
| The executable printed `Case not found: <case>`, for example because the case isn't compiled into this build. | Skipped | `[ SKIP ] Death test not present in this build configuration` |

The skip status is used only when nothing failed: a failure in the case, in
teardown or while writing reports takes precedence.

`EXTRA_ARGS` reach death tests too. An option that changes the run mode or the
repeat count, such as `--repeat=2`, makes the runner reject the death-test
command line, so every death test fails. The marker option is described in
[Command-line options](command_line.md).

## Run death tests yourself

```bash
./my_tests --list-death
./my_tests --include-death --run=crash/fatal_path
```

`--list-death` prints the death tests, except those with a static skip. The
second command runs one death test in the current process, which then ends the
way the test intends.

- Without `--include-death`, every selection leaves death tests out, and a
  `--run` that names one exits with status 1;
  [Test selection](test_selection.md) lists the messages.
- `--include-death` lets every selected death test run in the same process, so
  the first one ends the run. Run one death test per process.

Only CMake's `gentest_discover_tests()` registers death tests. With Meson,
Xmake or Bazel, a run of the test executable leaves them out; run each one
yourself with `--include-death --run=<case>`.

## Builds without exceptions

When your test code is compiled without exceptions, these end the process
instead of the case:

- a failed `ASSERT_*` check, or a failed `gentest::require*` check,
- `gentest::fail()`,
- `ASSERT_THROW`, which can't check for an exception in such a build.

gentest first tears down the case's local fixtures, then prints
`gentest: exceptions are disabled; terminating after fatal assertion (origin: <function>).`
to stderr and calls `std::terminate()`. The check's own message isn't printed.

`gentest::skip()` doesn't end the process in such a build: the case is reported
as skipped, but its local objects aren't destroyed and its local fixtures aren't
torn down.
To test these paths, mark them as death tests. To check the output, match the
line gentest prints:

```cpp
#pragma once

#include "gentest/test.h"

namespace noexc {

[[using gentest: test, death]]
inline void fatal_require() {
    gentest::require(false, "unreachable state");
}

} // namespace noexc
```

```cmake
# GCC and Clang. With MSVC, use /EHs-c- instead and also define _HAS_EXCEPTIONS=0.
target_compile_options(my_tests PRIVATE -fno-exceptions)
# fmt 11 and newer read FMT_USE_EXCEPTIONS; fmt 10 and older read FMT_EXCEPTIONS.
target_compile_definitions(my_tests PRIVATE FMT_USE_EXCEPTIONS=0 FMT_EXCEPTIONS=0)

gentest_discover_tests(my_tests DEATH_EXPECT_SUBSTRING "terminating after fatal assertion")
```

## Death tests that aren't compiled

A death test that the preprocessor removes in a configuration, for example
inside `#ifndef NDEBUG`, isn't in that configuration's `--list-json` inventory.
`gentest_discover_tests()` creates no CTest entry for it there.
