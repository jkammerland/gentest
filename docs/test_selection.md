# Test selection

The runner uses the same selector for execution and `--list-selected` previews.
Selectors apply to ordinary tests, benchmarks, and jitter cases:

```sh
./my_tests --kind=test --filter="unit/*" --tag=fast --tag=linux --exclude-tag=slow --exclude="*/database/*"
./my_tests --list-selected --kind=test --filter="unit/*" --tag=fast --tag=linux --exclude-tag=slow --exclude="*/database/*"
```

## Composition

Selection follows these rules:

1. `--run=<name>` resolves one full name or unique suffix among cases matching
   `--kind`. Otherwise, `--filter=<pattern>` matches names using `*` and `?`;
   omitting both selects all names. `--run` takes precedence over `--filter`
   when both are present.
2. `--kind=all|test|bench|jitter` restricts the selected case kinds.
3. When any `--tag=<tag>` options are present, keep cases with **any** requested
   tag. Repeating a tag has no additional effect.
4. Remove cases matching **any** `--exclude-tag=<tag>` or
   `--exclude=<name-pattern>`. Exclusions take precedence over includes,
   including a resolved `--run` name.
5. Remove death-tagged cases unless `--include-death` is present.

`--tag`, `--exclude-tag`, and `--exclude` are repeatable. Both `--tag=fast` and
`--tag fast` spellings work. Each option takes one nonempty value; empty or
missing values are CLI errors. A comma is part of a tag or pattern, not a list
separator.

Tag matching compares whole tags and ignores ASCII case: `--tag=FAST` matches
`fast`. It does not expand wildcards. Name matching is case-sensitive;
`--exclude="*/Slow*"` and `--exclude="*/slow*"` select different names. `*`
matches any number of characters, including namespace separators, and `?`
matches one character.

For example, `--tag=fast --tag=linux --exclude-tag=slow` keeps cases tagged
`fast` or `linux`, then removes every case also tagged `slow`. An untagged case
does not match an include tag, but remains eligible when there are only
exclusions. Kind and name restrictions intersect with this result.

## Previews and inventories

`--list-selected` prints selected names only, one per line, in deterministic
inventory order. It does not construct fixtures, run cases, write reports, or
shuffle names. Statically skipped cases remain visible because they are
selected cases whose execution outcome is a skip. Resolution errors, including
an ambiguous `--run`, are the same as execution errors.

`--list-tags` lists all distinct nonempty tags in sorted ASCII-lowercase form,
including tags on skipped and death cases. It ignores selection flags. The
other full-inventory modes, including `--list-tests`, `--list`, and
[`--list-json`](test_inventory.md), also ignore selectors. Listing modes cannot
be combined with `--list-selected` or `--list-tags`.

## Empty selections and death cases

An unknown include tag or an exclusion that removes every case produces an
empty selection. An empty selection succeeds by default and activates no
fixtures. `--list-selected` prints nothing; ordinary execution reports zero
selected cases. Add `--fail-on-empty` to make an empty selection fail with exit
code 1. A selected static or runtime skip does not count as empty.

Name-resolution errors have specific return codes: an unknown `--run` name
returns 3; an ambiguous name or a kind mismatch returns 1. An unmatched name filter
with an explicit `--kind=bench` or `--kind=jitter` is also an error, with a hint
to list available measured cases. Tag and exclusion filters applied to an
otherwise valid selection use the normal empty-selection policy.

`--tag=death` does not enable execution of death cases. A death-only tag or
wildcard selection is empty without `--include-death`; `--fail-on-empty` can
reject it. A resolved `--run` of a death case requires `--include-death` and
returns 1 without it. Explicit exclusions still remove death cases when
`--include-death` is enabled.

For process-isolated death execution, use
[`gentest_discover_tests`](discover_tests.md). It also propagates case tags to
CTest labels while retaining caller labels. Shared fixture activation for a
selected case is described in [fixture allocation](fixtures_allocation.md).
