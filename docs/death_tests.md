# Death tests

Use `death` to check that a test body aborts, crashes, or exits with a nonzero
status. Each death test runs in its own process.

```cpp
#include <cstdio>
#include <cstdlib>

namespace parser {

inline void check_length(int length) {
    if (length < 0) {
        std::fputs("negative length\n", stderr);
        std::abort();
    }
}

[[using gentest: test, death]]
inline void rejects_negative_length() {
    check_length(-1);
}

} // namespace parser
```

The case passes if the process dies during the body. A normal assertion failure,
a crash in fixture setup or teardown, and a timeout all fail the test. You can
also require text in the child's stdout or stderr to check that it reached the
intended failure.

The supervisor starts watching for termination after argument factories and
implicit conversions have finished. An exit while evaluating a default argument
or a `parameters(...)` expression fails the case. Death tests can take scalars,
references, and values whose transfer and destruction are trivial. Pass an
object with a user-defined move constructor or destructor by reference, for
example `void rejects_input(const Input &input = make_input())`. Default
expressions used by the generated call must be accessible from its namespace.
If a default uses a private member, supply a value with `parameters(...)`.

Each supervised death test starts with fresh fixtures, including suite and
global fixtures. In-memory changes made by earlier tests stay in their own
process. If the death test increments a fixture counter, the increment is lost
when the child exits. Other tests and reruns never see it.

Files and database writes can outlive the child. Arrange any required cleanup
outside the child because an abrupt exit can skip fixture teardown and destructors.

## Run a death test

`gentest_supervise` runs one named case and returns its result:

```sh
gentest_supervise --case parser/rejects_negative_length --expect "negative length" -- ./my_tests
```

To limit total runtime, add `--timeout-ms 5000` before `--`. To stop the case
when it stops making progress, use `--inactivity-ms 1000`. Long-running cases
can [report progress or request a quiet period](notifications.md#report-progress-from-a-case).
A skipped case returns 77.

With CMake, discover the cases once:

```cmake
gentest_discover_tests(my_tests)
```

Then run `ctest -R '^death/'`, or select one entry by name. To require output
for a group of cases, pass `DEATH_EXPECT_SUBSTRING` with a matching
`TEST_FILTER`. See [CTest integration](discover_tests.md#death-tests).

With [Bazel](buildsystems/bazel.md#death-tests), [Meson](buildsystems/meson.md#death-tests), or
[Xmake](buildsystems/xmake.md#death-tests), register the full case name with the build
system. The ordinary suite runs the other cases. A `tag` is optional.

## Debug the child directly

```sh
./my_tests --list-death
./my_tests --include-death --run=parser/rejects_negative_length
```

The second command runs the body in the foreground so you can use a debugger.
The process exits as the test intends.

In a [build without exceptions](assertions.md), a failed fatal assertion calls
`std::terminate()`. To check that path, mark the case with `death` and expect
the diagnostic `terminating after fatal assertion`.
