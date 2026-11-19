# Test selection

Use the selection options to run part of a test executable: one case, the cases
whose names match a pattern, or the cases with certain tags. The same rules
decide what a run executes and what `--list-selected` previews, for tests,
benchmarks and jitter cases alike.

```bash
./my_tests --run=math/add
./my_tests --filter='math/*' --kind=test
./my_tests --tag=fast --exclude-tag=slow --exclude='*/database/*'
./my_tests --list-selected --tag=fast --exclude-tag=slow --exclude='*/database/*'
```

The last command prints the names the third one would run, without running
them. [Command-line options](command_line.md#selecting-cases) has a one-line
summary of each option.

## How the options combine

The runner applies the options in this order:

1. **Names.** `--run=<name>` resolves to one case (see
   [Names and patterns](#names-and-patterns)). Otherwise `--filter=<pattern>`
   keeps the cases whose names match. Without either, every case is a
   candidate. When both are given, `--run` wins and `--filter` is ignored.
2. **Kind.** `--kind=all|test|bench|jitter` keeps only cases of that kind. The
   default is `all`.
3. **Tags.** When at least one `--tag=<tag>` is given, keep the cases that have
   **any** of the requested tags. Repeating a tag has no further effect.
4. **Exclusions.** Remove every case that has **any** `--exclude-tag=<tag>` tag
   or matches **any** `--exclude=<pattern>`. Exclusions win over every include,
   including the case `--run` resolved to.
5. **Death tests.** Remove the cases marked with the `death` attribute unless
   `--include-death` is given.

For example, `--tag=fast --tag=linux --exclude-tag=slow` keeps the cases tagged
`fast` or `linux`, then removes every one of them that is also tagged `slow`.
An untagged case never matches `--tag`, but it stays selected when you give
only exclusions.

## Names and patterns

Case names are the ones `--list-tests` prints, for example `math/add`. Every
name comparison is case-sensitive.

`--run=<name>` first looks for a case whose full name equals `<name>`. If there
is none, it accepts a suffix of a full name, as long as exactly one case of the
requested `--kind` ends with it. The suffix is a plain string suffix:
`--run=add` matches both `math/add` and `math/subadd`, so it fails as
ambiguous and the error lists both names. A `--run` whose matches all have
another kind fails too.

`--filter` and `--exclude` take wildcard patterns that must match the whole
name:

- `*` matches any number of characters, including `/`.
- `?` matches exactly one character.
- Every other character matches itself, including its case:
  `--exclude='*/Slow*'` and `--exclude='*/slow*'` select different names.

Parameterized names contain parentheses, commas, spaces and quotes. Quote
patterns and such names so the shell passes them unchanged, for example
`--run='math/add(2, "two words")'`.

A comma is part of an option's value, not a separator: repeat `--tag`,
`--exclude-tag` or `--exclude` to give several values.

## Tags

A tag matches only a whole tag, ignoring ASCII case: `--tag=FAST` selects a
case tagged `fast`. Tags don't take wildcards.

Declare labels with `tag("gpu", "integration")`. Generated and handwritten
cases can carry any labels, and none of their names has special behavior.
`owner(...)` and `req(...)` are separate metadata. [Writing tests](writing_tests.md)
describes the attributes.

`--list-tags` prints every distinct tag in the executable, in ASCII lowercase
and sorted, including the tags of skipped cases and death tests:

```text
fast
gpu
integration
slow
```

`--list-tags` ignores the selection options.

## Previewing a selection

`--list-selected` runs the selection and prints the selected names, one per
line, in inventory order (sorted by name). It doesn't set up fixtures, run
cases, write reports or shuffle. A case with a static skip appears too: it is
selected, and its outcome when run is a skip.

Selection errors such as an ambiguous `--run` are the same as for a real run.
An empty selection prints nothing and exits with `0`, or fails with
`--fail-on-empty`.

The other listing options, such as `--list-tests` and
[`--list-json`](test_inventory.md), ignore the selection options.

## Death tests

The runner removes death tests from every selection unless you pass
`--include-death`:

- Tags do not enable death tests. A tag or pattern that matches only
  death tests gives an empty selection, which `--fail-on-empty` rejects.
- A `--run` that resolves to a death test fails without `--include-death`:
  `Case 'crash/fatal_path' is a death test; rerun with --include-death`.
- When death tests are removed from a selection that keeps other cases, the run
  prints `Note: excluded 1 death test(s). Use --include-death to run them.`
  with the actual count.
- With `--include-death`, `--exclude-tag` and `--exclude` remove death tests as
  usual.

Run a selected death case with `gentest_supervise` to check its result.
See [Death tests](death_tests.md).

## Empty selections

A filter that matches nothing, an unknown include tag, or exclusions that
remove every case give an empty selection. By default an empty selection
succeeds: the run sets up no fixtures, prints a line such as
`Executed 0 case(s).` and exits with `0`, and `--list-selected` prints nothing. With `--junit`, the runner
writes a report for the empty run too; see [Reports](reports.md).

Add `--fail-on-empty` to make an empty selection an error, for example in CI:

```text
No cases selected (--kind=all, --filter=*); --fail-on-empty is enabled
```

A selected case that skips, statically or at run time, makes the selection
nonempty. A static skip sets up no fixtures; see [Fixtures](fixtures.md).

## Selection errors

These errors stop the run before any fixture is set up:

| Situation | Message |
| --- | --- |
| `--run` matches no case | `Case not found: <name>` |
| `--run` matches more than one case | `Case name is ambiguous: <name>`, followed by the matches |
| `--run` matches only cases of another kind | `Case '<name>' does not match --kind=<kind>` |
| `--run` resolves to a death test without `--include-death` | `Case '<name>' is a death test; rerun with --include-death` |
| `--filter` with `--kind=bench` matches no benchmark | `benchmark filter matched 0 benchmarks: <pattern>`, with a hint to use `--list-benches` |
| `--filter` with `--kind=jitter` matches no jitter case | `jitter filter matched 0 jitter benchmarks: <pattern>`, with the same hint |
| The selection is empty and `--fail-on-empty` is given | `No cases selected (...); --fail-on-empty is enabled` |

`--tag`, `--exclude-tag` and `--exclude` never cause the two filter errors: when
they empty an otherwise valid selection, the empty-selection rules above apply.
[Exit codes](command_line.md#exit-codes) gives the status each error returns.

## CTest

[`gentest_discover_tests()`](discover_tests.md) registers every case as its
own CTest entry and runs each death test in its own process. It turns each case
tag into a CTest label and keeps the labels you pass, so `ctest -L fast`
selects the cases tagged `fast`. Its `TEST_FILTER` and `FAIL_ON_EMPTY`
arguments work like `--filter` and `--fail-on-empty` at CTest discovery time.
