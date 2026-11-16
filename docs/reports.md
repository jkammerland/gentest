# Reports

Write the results of a test run in formats that CI systems read: JUnit XML,
Allure results and GitHub Actions annotations. This page also defines how each
case outcome, including **blocked**, appears in those formats.

## Write a report

Pass a report option to the test executable. You can combine them in one run:

```bash
./my_tests --junit=junit.xml
./my_tests --allure-dir=allure-results
./my_tests --github-annotations
./my_tests --junit=junit.xml --allure-dir=allure-results
```

For example, take these four cases: one passes, one fails, one is skipped and
one is an expected failure.

```cpp
#pragma once

#include "gentest/test.h"

using namespace gentest::asserts;

namespace math {

[[using gentest: test("add"), req("SSR-001")]]
inline void add() {
    EXPECT_EQ(1 + 1, 2);
}

[[using gentest: test("divide"), req("SSR-002")]]
inline void divide() {
    EXPECT_EQ(7 / 2, 4);
}

[[using gentest: test("legacy_api"), skip("replaced by add")]]
inline void legacy_api() {}

[[using gentest: test("rounding")]]
inline void rounding() {
    gentest::xfail("tracked in BUG-42");
    EXPECT_EQ(static_cast<int>(2.5), 3);
}

} // namespace math
```

`./my_tests --junit=junit.xml` exits with 1, because `math/divide` fails, and
writes:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="gentest" tests="4" failures="1" skipped="2" errors="0">
  <testcase classname="math" name="math/add" time="0.000001694">
    <properties>
      <property name="requirement" value="SSR-001"/>
    </properties>
  </testcase>
  <testcase classname="math" name="math/divide" time="0.000066128">
    <properties>
      <property name="requirement" value="SSR-002"/>
    </properties>
    <failure><![CDATA[EXPECT_EQ    failed at src/demo/cases.hpp:16: lhs=3, rhs=4]]></failure>
  </testcase>
  <testcase classname="math" name="math/legacy_api" time="0.000000000">
    <skipped message="replaced by add"/>
  </testcase>
  <testcase classname="math" name="math/rounding" time="0.000020775">
    <skipped message="xfail: tracked in BUG-42"/>
  </testcase>
</testsuite>
```

Each `req(...)` ID becomes a `requirement` property; see
[Requirements traceability](traceability_standards.md) for building a trace
matrix from it.

## Outcomes in reports

Every case [occurrence](command_line.md#running-cases) ends with one of the
outcomes below. Infrastructure errors belong to the run, not to a case.

| Outcome | Meaning | JUnit | Allure `status` |
|---|---|---|---|
| pass | The case ran and recorded no failure. | a `<testcase>` with no result element | `passed` |
| fail | An assertion or expectation failed, or the case threw. | one `<failure>` per failure message, counted in `failures` | `failed` |
| XPASS | The case called `gentest::xfail()` but did not fail. | `<failure>` with the text `xpass` or `xpass: <reason>`, counted in `failures` | `failed` |
| skip | A [static skip](assertions.md) (`skip("...")` attribute) or a runtime `gentest::skip()`. | `<skipped>` with the reason as `message`, counted in `skipped` | `skipped` |
| xfail | The case called `gentest::xfail()` and then failed, as expected. | `<skipped>` with the message `xfail` or `xfail: <reason>`, counted in `skipped` | `skipped`, with an `xfail` label |
| blocked | The case could not run or finish: a suite or global fixture it needs failed to set up, or an async case awaited a promise that was marked with `set_blocked()` or dropped unfinished. | `<error type="blocked">` with the reason as `message`, counted in `errors` | `broken`, with `blocked` and `blocked_reason` labels |
| infrastructure error | Not a case. A runner-level error, such as a shared fixture setup or teardown failure, a report that cannot be written, or an invocation that failed before any case ran. | the message in `<system-err>`, counted in `errors` | a separate `failed` result named `gentest/infra_error/<n>` in the suite `gentest/infra` |

Fail, XPASS, blocked and infrastructure errors make the run exit with 1; pass,
skip and xfail don't (see [exit codes](command_line.md#exit-codes)). When a
shared fixture fails to set up, the setup failure is an infrastructure error
and each case that needs the fixture is blocked.

## JUnit XML

`--junit=<file>` writes one `<testsuite name="gentest">` element with `tests`,
`failures`, `skipped` and `errors` counts:

- Each case occurrence is one `<testcase>`. Its `classname` is the case's
  suite and its `name` is the full case name. With `--repeat`, every occurrence
  is a separate `<testcase>`.
- `time` is in seconds, written in fixed-point notation with nine decimals.
  Times and counts don't depend on the process locale.
- `<properties>` holds a `requirement` property for each `req(...)` ID. It also
  holds `gentest.property.<key>` for runtime properties and `gentest.records`
  for the runtime-record index; see [Recording](recording.md).
- Text that is not valid UTF-8, or characters that XML 1.0 can't represent,
  become `?`.

The directory for the file must exist. If the runner can't open or write the
file, the run reports an infrastructure error, for example
`failed to open JUnit report: out/junit.xml`, and exits with 1.

## Allure results

`--allure-dir=<dir>` writes Allure 2 result files that the Allure report
generator reads directly. The runner creates the directory if it doesn't exist.

### Files and shared directories

| File | Written for |
|---|---|
| `<token>-<n>-result.json` | each case occurrence and each infrastructure error |
| `<token>-<n>-<name>-attachment.<ext>` | each attachment of result `<n>` |
| `<token>-record-*`, `<token>-case-<id>-index.json` | runtime records; see [Recording](recording.md) |

`<token>` is a random UUID made once per run, and `<n>` numbers the results of
that run. Because of the token, any number of executables, per-case CTest runs
or `run_all_tests()` calls can share one directory. The runner never deletes or
rewrites a file it did not write in that run, so empty the directory before a
run whose report should contain only that run.

### Result names and history

Each result's `name` is the case name, and its `fullName` is
`<program>:<case>`. The program is the executable's file name without
directory or extension, for example `my_tests:math/add`. A `parentSuite` label
holds the program, and a `suite` label holds the case's suite.

`historyId` and `testCaseId` are a hash of `fullName`. So same-named cases of
different programs stay separate tests in one report, reruns of one program
link into one history, and the occurrences of a `--repeat` run appear as
retries. When an [embedding program](runner_api.md) passes no program name,
`fullName` is the case name and there is no `parentSuite` label.

### Result contents

- `status` follows the [outcome table](#outcomes-in-reports). `statusDetails`
  holds the first failure message, or the reason of a skipped, expected-failure
  or blocked case. When a case has several failure messages, `trace` holds all
  of them.
- `start` and `stop` are Unix times in milliseconds. A case that ran lasts at
  least 1 ms.
- Labels: `parentSuite`, `suite`, `framework` (`gentest`), `language` (`cpp`),
  one `tag` label per tag and one `requirement` label per `req(...)` ID. An
  expected failure adds `xfail` with the reason; a blocked case adds `blocked`
  (`true`) and `blocked_reason`.
- Parameters: runtime properties, named `gentest.property.<key>`.
- Attachments: `logs` (the case's captured `gentest::log()` messages),
  `timeline` (its failure messages, in order), runtime records, and the
  measured-case attachments below.
- Text that is not valid UTF-8 is replaced with U+FFFD.

### Measured cases in Allure

When a bench or jitter case passes, its result also carries these attachments:

| Attachment | Format | Bench | Jitter | Content |
|---|---|---|---|---|
| `metrics` | TSV | yes | yes | One `metric`/`value` row per statistic and setting. |
| `summary-plot` | SVG | yes | | A plot of the timing summary. |
| `histogram` | TSV | | yes | The histogram bins; `--jitter-bins` sets their number. |
| `histogram-plot` | SVG | | yes | A plot of the histogram. |
| `samples` | JSON | | yes | The raw samples, at most 2048 evenly spaced values; `truncated` is `true` when the runner dropped some. |
| `measurements` | TSV | yes | yes | Named timing series that the case published with `gentest::recordTimingSamples()`; only when it published any. |

Timing metric names end in one of these suffixes:

- `*_ns_per_call`: nanoseconds per function call.
- `*_ns_per_item`: the per-call value divided by the case's `items_per_call`,
  or by the item count a benchmark passes to `gentest::setBenchmarkDuration()`.

For example, a 10 ns call that processes two items has
`median_ns_per_call=10` and `median_ns_per_item=5`.
Jitter metrics also include `mode`, `sample_kind` and `calls_per_sample`, plus
`sampling_warning` when each sample averages several calls. See
[Benchmarks](benchmarks.md) for what these measurements mean.

## GitHub Actions annotations

`--github-annotations` prints one `::error` workflow command per failure at the
end of the run, which GitHub Actions shows as an annotation. For the failing
case in the first example:

```text
::error file=src/demo/cases.hpp,line=16,title=math/divide::EXPECT_EQ    failed at src/demo/cases.hpp:16: lhs=3, rhs=4
```

- Annotations are on automatically when the `GITHUB_ACTIONS` environment
  variable is set, as it is in every GitHub Actions step.
- File paths are shortened to start at the first `tests/`, else `include/`,
  `src/` or `tools/` directory, checked in that order:
  `/home/me/src/repo/tests/a.hpp` becomes `tests/a.hpp`. Other paths are
  printed in full.
- The lines go to stdout. With `--report-format=json` or `--report-format=csv`
  they go to stderr, so that stdout holds only the measured report.
- The title is the case name. The location is the failing assertion's file and
  line when known, otherwise the case's declaration.
- The runner annotates each failed assertion or expectation, an exception that
  ended a case, an XPASS, a blocked async case and a failed bench or jitter
  case. Cases blocked by a shared fixture and infrastructure errors get no
  annotation.
- In the message, `%`, carriage return and line feed are percent-encoded. In
  `file` and `title`, `:` and `,` are encoded as well.

## When reports are written

The runner writes JUnit and Allure files when the run ends, after shared
fixture teardown:

- A run that selects no cases writes them too; the JUnit file then has
  `tests="0"`.
- An invocation that fails before any case runs, for example on a
  [selection error](test_selection.md#selection-errors), also writes the
  requested reports, so a report left by an earlier run is never mistaken for
  this one. The JUnit file has no test cases and holds the error in
  `<system-err>`, and Allure gets one infrastructure-error result.
- An invalid command line, such as an unknown option or a bad value, is
  rejected before any report path is used. Existing files stay as they are.
- `--help` and the listing options, including `--list-selected`, never write
  reports.
- A report that can't be written is an infrastructure error, and the other
  report records it: the JUnit file lists an Allure write failure, and Allure
  gets a result for a JUnit write failure.

## Reports from CTest runs

[`gentest_discover_tests()`](discover_tests.md) registers one CTest test per
case, and each one runs the executable with `--run=<case>`. Pass report options
with `EXTRA_ARGS`. All per-case runs can write into one Allure directory:

```cmake
gentest_discover_tests(my_tests
    EXTRA_ARGS --allure-dir=${CMAKE_BINARY_DIR}/allure-results)
```

Don't pass a fixed `--junit` path this way: each per-case run replaces the
file. For JUnit, run the executable once with `--junit`, or let CTest write the
file with `ctest --output-junit <file>`.

## CDash dashboard for the gentest repository

`cmake/cdash/Experimental.cmake` configures, builds and tests a gentest
checkout and can submit the results to a CDash server. Run it from the
checkout:

```bash
ctest -S cmake/cdash/Experimental.cmake -VV
GENTEST_CDASH_ENABLE_ALLURE_TESTS=ON ctest -S cmake/cdash/Experimental.cmake -VV
GENTEST_CDASH_SUBMIT_URL='https://cdash.example/submit.php?project=gentest' ctest -S cmake/cdash/Experimental.cmake -VV
```

Without a submit URL, it runs the steps and submits nothing. It always
configures with `-Dgentest_BUILD_TESTING=ON`, `-DGENTEST_BUILD_CODEGEN=ON` and
`-DGENTEST_ENABLE_PACKAGE_TESTS=OFF`, and a failed step stops it with a
`gentest-cdash:` error. These environment variables change its settings:

| Variable | Default | Effect |
|---|---|---|
| `GENTEST_CDASH_SOURCE_DIR` | the checkout that holds the script | Source tree to configure. |
| `GENTEST_CDASH_BINARY_DIR` | `<source>/build/cdash-experimental` | Build tree. |
| `GENTEST_CDASH_MODEL` | `Experimental` | Dashboard model for `ctest_start()`. |
| `GENTEST_CDASH_GROUP` | the model | Dashboard group. |
| `GENTEST_CDASH_GENERATOR` | `Ninja` | CMake generator. |
| `GENTEST_CDASH_CONFIGURATION` | `Debug` | `CMAKE_BUILD_TYPE` and build configuration. |
| `GENTEST_CDASH_PARALLEL_LEVEL` | `8` | Parallel level for the build and test steps. |
| `GENTEST_CDASH_ENABLE_ALLURE_TESTS` | `OFF` | Passed as `GENTEST_ENABLE_ALLURE_TESTS`, which turns on the repository's Allure writer tests. `1`, `ON`, `TRUE` and `YES` count as on. |
| `GENTEST_CDASH_BUILD_TARGETS` | all targets | Comma-separated list of targets to build. |
| `GENTEST_CDASH_TEST_REGEX` | all tests | CTest include regular expression. |
| `GENTEST_CDASH_CONFIGURE_OPTIONS` | none | Extra configure arguments, separated by spaces. |
| `GENTEST_CDASH_CONFIGURE_OPTIONS_FILE` | none | File with one extra configure argument per line. |
| `GENTEST_CDASH_SUBMIT_URL` | none | CDash submit URL. `CTestConfig.cmake` reads it too. |
| `GENTEST_CDASH_DRY_RUN` | `OFF` | Write the settings to `cdash-dry-run.txt` in the build tree and stop before configuring. `1`, `ON`, `TRUE` and `YES` count as on. |

The site name defaults to the host name, and the build name to
`<site>-<model>-<configuration>-<generator>`.

Your own project's cases reach CDash like any other CTest tests: register them
with `gentest_discover_tests()` and submit them with your own dashboard script.
