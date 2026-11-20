# CTest integration (`gentest_discover_tests`)

`gentest_discover_tests()` registers cases from a test executable as individual
CTest entries. You can run them by name or label and run several in parallel.

```cmake
include(CTest)
find_package(gentest CONFIG REQUIRED)

add_executable(my_tests cases.hpp)
target_compile_features(my_tests PRIVATE cxx_std_20)
target_link_libraries(my_tests PRIVATE gentest::gentest_main)
gentest_attach_codegen(my_tests)
gentest_discover_tests(my_tests)
```

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Discovery reads the case list from `my_tests --list-json`. For rerunning failed
entries, parallel runs and repetition, see
[CTest workflows](runner_workflows.md#ctest-workflows).

## What it registers

- **Ordinary cases.** The CTest name is the case name, between `TEST_PREFIX`
  and `TEST_SUFFIX`. Each entry selects its exact case name. If that name is
  missing or names a death test at execution time, the entry fails.
- **Death tests.** Discovery registers each runnable `death` case with a
  `death/` prefix after `TEST_PREFIX`, for example `death/crash/fatal_path`.
  Each entry runs `gentest_supervise`. Discovery omits statically skipped death
  cases. See [Death tests](death_tests.md).

For both kinds of entry, if `--tag`, `--exclude-tag`, or `--exclude` in
`EXTRA_ARGS` filters out the case, CTest reports a skip (return code 77).
Add `--fail-on-empty` to fail the entry when these filters exclude it.

Case names are registered verbatim, including semicolons and brackets. Only the
cases compiled into the executable appear: a case that the preprocessor removes
in one configuration (for example inside `#ifndef NDEBUG`) gets no CTest entry
in that configuration.

If the target's `TEST_LAUNCHER` or `CROSSCOMPILING_EMULATOR` property is set,
CTest discovery and every registered entry run the executable through it, the
launcher first.

## Arguments

```cmake
gentest_discover_tests(<target>
  [TEST_PREFIX <prefix>]
  [TEST_SUFFIX <suffix>]
  [TEST_FILTER <pattern>]
  [FAIL_ON_EMPTY]
  [WORKING_DIRECTORY <dir>]
  [TEST_LIST <variable>]
  [DISCOVERY_TIMEOUT <seconds>]
  [DISCOVERY_MODE <POST_BUILD|PRE_TEST>]
  [EXTRA_ARGS <arg>...]
  [DISCOVERY_EXTRA_ARGS <arg>...]
  [PROPERTIES <name> <value>...]
  [DEATH_EXPECT_SUBSTRING <text>]
  [DEATH_TIMEOUT_MS <ms>] [DEATH_INACTIVITY_MS <ms>])
```

| Argument | Meaning |
| --- | --- |
| `<target>` | The executable target to ask for cases. It must exist and be an executable. |
| `TEST_PREFIX <prefix>` | Text added before each CTest name, before the `death/` prefix of death tests. |
| `TEST_SUFFIX <suffix>` | Text added after each CTest name. |
| `TEST_FILTER <pattern>` | Registers only the cases whose names match this wildcard pattern (`*`, `?`). See [Filtering](#filtering). |
| `FAIL_ON_EMPTY` | Fails CTest discovery when no entry is registered. Off by default. |
| `WORKING_DIRECTORY <dir>` | Working directory for CTest discovery and for every entry. Default: `CMAKE_CURRENT_BINARY_DIR`. |
| `TEST_LIST <variable>` | Name of the variable that receives the registered CTest names. Default: `<target>_TESTS`. See [The test list variable](#the-test-list-variable). |
| `DISCOVERY_TIMEOUT <seconds>` | Time limit for the `--list-json` run. Default: `5`. |
| `DISCOVERY_MODE <mode>` | When CTest discovery runs: `POST_BUILD` (default) or `PRE_TEST`. See [Discovery modes](#discovery-modes). |
| `EXTRA_ARGS <arg>...` | Arguments appended to every registered command, for example `--no-color`. A shared `--allure-dir` works; a fixed `--junit` path is overwritten by every entry (see [Reports from CTest runs](reports.md#reports-from-ctest-runs)). |
| `DISCOVERY_EXTRA_ARGS <arg>...` | Arguments appended to the CTest discovery command, after `--list-json`. |
| `PROPERTIES <name> <value>...` | CTest properties set on every entry. Case tags are added to `LABELS`; your labels stay. |
| `DEATH_EXPECT_SUBSTRING <text>` | Text that each death test registered by this call must print. See [Death tests](#death-tests). |
| `DEATH_TIMEOUT_MS <ms>` | Maximum duration of each death test. |
| `DEATH_INACTIVITY_MS <ms>` | Maximum time without progress in a death test. |

`EXTRA_ARGS` must be options the executable accepts. Don't pass `--run` or
`--skip-return-code` there. Discovery supplies the case selection and skip return code.

## Filtering

`TEST_FILTER` uses the same wildcards as the runner's `--filter` (`*` matches
any characters including `/`, `?` one character). The pattern must match the
whole case name, is case-sensitive, and is matched before `TEST_PREFIX` and the
`death/` prefix are added: `TEST_FILTER "death/*"` selects the cases whose own
names start with `death/`, not the death tests.

For CI, make an accidental empty registration an error:

```cmake
gentest_discover_tests(my_tests TEST_FILTER "unit/*" FAIL_ON_EMPTY)
```

With `FAIL_ON_EMPTY`, CTest discovery stops with
`gentest_discover_tests: no cases registered for '<executable>' (TEST_FILTER='<pattern>'); FAIL_ON_EMPTY is enabled`
when nothing is registered. A matching case with a static skip counts; a death
test with a static skip doesn't, because it isn't registered.

You can call `gentest_discover_tests()` more than once for the same target, for
example with different filters, prefixes or properties. Make the calls
select disjoint cases: a case that two calls register gets two CTest entries.

## Labels

Each nonempty user-defined tag becomes a CTest label with the tag's spelling.
Owner metadata and the death-test flag do not add labels. Labels you pass with
`PROPERTIES` stay:

```cmake
gentest_discover_tests(my_tests PROPERTIES LABELS "project;unit")
```

A case tagged `fast` gets the labels `project`, `unit` and `fast`, so
`ctest -L fast` selects it.

## Death tests

`DEATH_EXPECT_SUBSTRING <text>` requires each registered death test to print
`<text>` to stdout or stderr. To check different messages, register each group
with its own `TEST_FILTER`. See [Death tests](death_tests.md) for the pass criteria.

When cross-compiling, set `GENTEST_SUPERVISE_EXECUTABLE` to the host tool if it
is outside the host codegen directory and search path.

## Discovery modes

| Mode | When CTest discovery runs |
| --- | --- |
| `POST_BUILD` | After the executable is linked. A discovery failure fails the build. This is the default. |
| `PRE_TEST` | When CTest starts and the saved list is missing or older than the executable. Use it when the executable can't run at build time. |

Set `CMAKE_GENTEST_DISCOVER_TESTS_DISCOVERY_MODE` to change the default for
calls that don't pass `DISCOVERY_MODE`.

With multi-config generators, each configuration gets its own list, and
`ctest -C <config>` uses that configuration's list. If CTest runs before the
executable exists, CTest shows a placeholder entry named `<target>_NOT_BUILT`;
build the target first.

If the executable fails or exceeds `DISCOVERY_TIMEOUT`, CTest discovery stops
with `Error running test executable.` followed by the executable's path,
working directory, result and output.

## The test list variable

`TEST_LIST` names a variable that holds the registered CTest names. The
variable is set when CTest reads the generated test file, so it is available to
CTest scripts such as those added through the `TEST_INCLUDE_FILES` directory
property, not during configuration. Names containing `[` or `]`, or ending in a
backslash, are left out of the variable because CMake lists can't hold them
reliably, but their CTest entries are registered as usual.
