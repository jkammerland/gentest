# Story: Linux-Only Coverage Simplification And Cross-Platform WIP Backup

## Goal

Preserve the current cross-platform coverage and lint follow-up work so it can
be resumed later, while simplifying the active branch back to a Linux-only
coverage contract for both CI and the documented local workflow.

The backup branch for the preserved work is:

`backup/coverage-cross-platform-wip-20260404`

## Current status

This repository now has a useful Linux coverage gate and report flow:

- `coverage-system` already exists as the execution preset
- the Linux CI lane can run coverage serially and publish scoped reports
- the intended coverage surface is repo-owned implementation under `src/` and
  `tools/src/`

The churn started when the same work was extended to cover Windows-oriented
tool and path handling in shell/Python helper scripts.

That cross-platform follow-up touched:

- `scripts/coverage_report.py`
- `scripts/coverage_hygiene.py`
- `scripts/check_clang_tidy.sh`
- workflow contracts and smoke tests under `cmake/`
- README / AGENTS / coverage docs

## Why review kept finding issues

The repeated review feedback was not random. The branch accumulated several
layers of platform heuristics in places that are naturally fragile:

1. shell + Python parsing of `compile_commands.json`
2. manual rewriting of Windows drive-path command strings
3. serialization of composite commands such as `llvm-cov gcov`
4. generated-target discovery by scraping build artifacts

Those heuristics were increasingly Windows-aware, but the behavioral coverage
was still mostly Linux-hosted or synthetic. That mismatch is why batch review
kept surfacing "one more Windows edge case" instead of converging quickly.

## Recommended simplification

For the active branch, the coverage contract should be intentionally narrowed:

1. CI coverage remains Linux-only.
2. The documented local coverage flow remains Linux/POSIX-only.
3. Windows/macOS coverage support is not claimed in docs, contracts, or smoke
   tests for this slice of work.
4. Keep the Linux coverage reporting gate itself.
5. Keep any already-needed Linux lint fixes that are independent of the
   coverage scope decision.

## What should likely stay on the active branch

- the Linux coverage report/gate integrated with the existing coverage lane
- coverage scoping to repo-owned implementation files
- coverage HTML / JSON / summary outputs
- Linux/POSIX local workflow documentation
- workflow contract checks for the Linux coverage lane

## What should likely move out of scope

- Windows-specific `gcov` / `llvm-cov gcov` command serialization support in
  the coverage reporting flow
- Windows-specific coverage smoke/contract expectations
- synthetic Windows parsing work added only to justify future Windows coverage
  support
- docs that imply Windows coverage is a supported local workflow today

## Follow-up when cross-platform coverage is revisited

If Windows coverage becomes a real goal later, it should not be resumed as
string-handling tweaks alone. The follow-up should first define:

1. a maintained canonical Windows workspace on `B:\\repos\\gentest`
2. a native Windows behavioral test loop for the coverage helpers
3. the exact supported compiler + gcov frontend combinations
4. whether `compile_commands.json` parsing remains shell/Python-based or moves
   to a more structured implementation
