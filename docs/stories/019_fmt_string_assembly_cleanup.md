# Story: Replace chained `std::string::append()` assembly with `fmt`

## Problem

The codebase still has a lot of string-building logic that grows output step by
step with repeated `.append()` calls, `+=`, and hand-managed separators.

That has a few costs:

- formatting intent is spread across many statements
- delimiter and quoting rules are easy to get wrong
- review noise is high when output shape changes
- some call sites mix user-facing formatting, command-line assembly, JSON-ish
  assembly, and path/report rendering in an ad hoc way

We already depend on `fmt`, so the current style is carrying complexity without
buying us a real dependency reduction.

## User stories

As a maintainer, I want string formatting sites to express the final output
shape directly, so output changes are easier to review and validate.

As a contributor, I want command-line, report, and diagnostic text assembly to
use one consistent formatting style, so I do not have to manually reason about
separator placement and partial append order.

As a reviewer, I want string-building code to be obviously correct at a glance,
so correctness issues are less likely to hide inside long append chains.

## Scope

Convert string assembly that is fundamentally formatting-oriented to `fmt`:

- user-facing diagnostics and status output
- report/JUnit/XML/JSON-ish text assembly
- codegen-emitted source/header/module text assembly where full template
  replacement is not already used
- command-line fragments and shell-ish argument rendering where the output is
  already textual and not a mutable token container

Out of scope:

- containers that are not really string formatting
- binary buffers
- hot-path cases where `fmt::memory_buffer` or `fmt::format_to` is needed
  instead of `fmt::format`
- places where existing template files are already the clearer abstraction

## Design direction

Preferred replacements:

- simple one-shot strings: `fmt::format(...)`
- incremental output in loops: `fmt::memory_buffer` + `fmt::format_to(...)`
- append-to-existing-string sites: `fmt::format_to(std::back_inserter(out), ...)`

Avoid replacing one append chain with many small `fmt::format(...)` temporary
strings. The point is to make the final output clearer, not to reshuffle the
same fragmentation behind a new API.

## Rollout

1. Inventory all `.append()` / `+=` string-assembly hotspots.
2. Classify them by kind:
   - diagnostics
   - reporting/serialization
   - codegen text
   - command-line assembly
3. Convert the clearest low-risk sites first.
4. Add or tighten regression coverage around any output-sensitive path before
   changing formatting-heavy logic.
5. Leave performance-sensitive builders on `fmt::memory_buffer` /
   `format_to(...)`, not naive `fmt::format(...)` loops.

## Acceptance criteria

- Chained `.append()` string assembly is substantially reduced in formatting
  code.
- New or changed output-sensitive sites keep existing behavior unless a test or
  intentional output change says otherwise.
- The migration does not regress quoting, separators, or newline behavior in
  diagnostics, reports, or codegen outputs.
- Performance-sensitive paths are reviewed explicitly instead of being converted
  mechanically.
