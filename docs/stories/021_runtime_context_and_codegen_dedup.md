# Closed Story Record: dedup runtime test-context plumbing and generated output naming

## Goal

Remove low-value duplication in two internal areas without changing user-facing behavior:

1. repeated runtime test-context lifecycle plumbing in executor paths
2. repeated generated output naming logic in codegen

## Status

Closed and implemented in commit `c03c6535`.

This document is the implementation record for the shipped slice:

1. shared runtime test-context helpers landed
2. tool-side generated output naming was centralized
3. test-side codegen backend resolution was deduplicated

The remaining open follow-up from this slice is CMake/tool output-name parity,
which is tracked separately in `029_codegen_output_naming_contract_unification.md`.

This file is retained as an implementation record, not as active backlog.

## User stories

As a maintainer, I want repeated runtime test-context setup and teardown logic to
live behind one private helper shape, so execution-path fixes do not need to be
copied across multiple files.

As a contributor, I want tool-side generated output naming to come from one
shared helper, so depfile and emission behavior cannot silently drift apart.

As a reviewer, I want this cleanup to stay internal and behavior-preserving, so
the diff removes duplication without reopening public API or reporting semantics.

## Scope

### Runtime

Unify repeated pattern used by:

1. `src/runner_case_invoker.cpp`
2. `src/runner_measured_executor.cpp`
3. `src/runner_fixture_runtime.cpp`

Pattern today:

1. allocate `TestContextInfo`
2. set display name
3. mark active
4. install current context
5. run body
6. wait for adopted contexts
7. flush thread-local buffers
8. deactivate context
9. clear or restore current context

Target shape:

1. one private helper for context creation
2. one private RAII scope for install/finalize/restore
3. callers keep their own outcome classification logic

### Codegen

Unify tool-side generated stem + module-wrapper output path logic currently duplicated in:

1. `tools/src/main.cpp`
2. `tools/src/emit.cpp`

Target shape:

1. one private helper header
2. both emit and depfile paths use same function
3. CMake-side output-name duplication moves to story `029`

### CMake

Remove local test-side duplication of codegen backend resolution in `tests/CMakeLists.txt` and reuse `_gentest_resolve_codegen_backend()`.

## Non-goals

1. public API changes
2. behavior changes in skip/fail/report semantics
3. larger fixture-runtime redesign
4. deleting manifest mode

## Validation

1. build `debug-system`
2. run focused runtime + codegen tests
3. run batch Codex review on diff before commit
