# Test inventory

Use `--list-json` when a tool, such as an IDE, a dashboard or a CI script, needs
a stable, machine-readable list of the cases compiled into a test executable.

```bash
./my_tests --list-json > inventory.json
```

For these cases:

```cpp
#pragma once

#include "gentest/test.h"

using namespace gentest::asserts;

namespace math {

struct [[using gentest: fixture(suite)]] Tables {
    int size = 8;
};

[[using gentest: test("add"), req("SSR-001"), owner("team-runtime"), fast]]
inline void add() {
    EXPECT_EQ(1 + 1, 2);
}

[[using gentest: test("lookup")]]
inline void lookup(Tables &tables) {
    EXPECT_EQ(tables.size, 8);
}

[[using gentest: test("legacy_api"), skip("replaced by add")]]
inline void legacy_api() {}

} // namespace math
```

the command writes one JSON array, with one object per case on its own line:

```json
[
  {"name":"math/add","file":"cases.hpp","line":14,"kind":"test","tags":["owner=team-runtime","fast"],"requirements":["SSR-001"],"owner":"team-runtime","skipped":false,"skipReason":"","fixture":"","fixtureLifetime":"none","suite":"math","async":false,"baseline":false,"itemsPerCall":1},
  {"name":"math/legacy_api","file":"cases.hpp","line":24,"kind":"test","tags":[],"requirements":[],"owner":"","skipped":true,"skipReason":"replaced by add","fixture":"","fixtureLifetime":"none","suite":"math","async":false,"baseline":false,"itemsPerCall":1},
  {"name":"math/lookup","file":"cases.hpp","line":19,"kind":"test","tags":[],"requirements":[],"owner":"","skipped":false,"skipReason":"","fixture":"math::Tables","fixtureLifetime":"suite","suite":"math","async":false,"baseline":false,"itemsPerCall":1}
]
```

The cases appear in the same order as in the other listings: sorted by name,
then by file and line.

## Fields

Every object has all of these keys:

| Key | JSON type | Meaning |
|---|---|---|
| `name` | string | Full case name, as `--run` and the reports use it. |
| `file` | string | File that declares the case. For a generated case, the path is relative to the project's top-level source directory when the file is inside it. |
| `line` | number | Line of the declaration. |
| `kind` | string | `test`, `bench` or `jitter`. |
| `tags` | array of strings | Tags from the flag attributes `fast`, `slow`, `linux`, `windows` and `death`, plus `owner=<owner>` when the case has an `owner(...)`. |
| `requirements` | array of strings | IDs from `req(...)`. |
| `owner` | string | Value of `owner(...)`; empty when the case has none. |
| `skipped` | boolean | `true` for a [static skip](assertions.md). |
| `skipReason` | string | Reason given to `skip(...)`; empty when there is none. |
| `fixture` | string | Qualified type name of the suite or global fixture the case shares, or of the class that declares a member test; empty for other cases. |
| `fixtureLifetime` | string | `none`, `local`, `suite` or `global`; see the next table. |
| `suite` | string | The suite the case belongs to. |
| `async` | boolean | `true` for a coroutine case; see [Async tests](async.md). |
| `baseline` | boolean | `true` for a bench or jitter case marked `baseline`. |
| `itemsPerCall` | number | Logical items per call (`items_per_call`) of a bench or jitter case; `1` unless set. |

The owner also appears in `tags` as `owner=<owner>`, so tools that read only
tags, such as CTest labels and Allure `tag` labels, see it as well.

### `fixtureLifetime` values

The runner groups the cases that share a suite or global fixture by the name
in `fixture`. This table maps each `fixtureLifetime` value to the fixture
lifetime it stands for and to the matching `gentest::FixtureLifetime` value of
a `gentest::Case`:

| `fixtureLifetime` | `gentest::FixtureLifetime` | Meaning |
|---|---|---|
| `none` | `None` | No shared fixture groups the case. The case may take local fixtures as parameters. |
| `local` | `MemberEphemeral` | A member test of a class without a `fixture(...)` attribute; each case gets its own instance. Member tests are deprecated; see [DEPRECATIONS.md](../DEPRECATIONS.md). |
| `suite` | `MemberSuite` | The case shares a suite fixture, as a parameter or as a member test of a `fixture(suite)` class. |
| `global` | `MemberGlobal` | The case shares a global fixture. |

When a case takes several suite or global fixtures, `fixture` names the first
one in parameter order. See [Fixtures](fixtures.md) for the lifetimes, and
[Embedding the runner](runner_api.md) for `gentest::Case`.

## What the inventory covers

- It lists every compiled case, including skipped and death cases. Selection
  options such as `--filter`, `--tag` and `--kind` don't change it. To preview
  which cases a run would select, use `--list-selected`; see
  [Test selection](test_selection.md).
- It describes the compiled cases, not their results. For outcomes, join it
  with a [JUnit or Allure report](reports.md) on `name`.
- Listing runs no fixtures and no cases, writes only the JSON array to stdout,
  and exits with 0.
- Bytes that are not valid UTF-8 become U+FFFD.
- [CTest discovery](discover_tests.md) reads this output to register one CTest
  test per case.

For a human-readable listing of the same metadata, use `--list`:

```text
math/add [gentest:tags=owner=team-runtime,fast;requires=SSR-001;owner=team-runtime] (cases.hpp:14)
math/legacy_api [gentest:skip=replaced by add] (cases.hpp:24)
math/lookup (cases.hpp:19)
```

See [Command line](command_line.md) for the other listing options.
