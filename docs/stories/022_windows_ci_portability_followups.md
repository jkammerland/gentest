# Story: Windows CI Portability Follow-Ups

## Goal

Stabilize the remaining Windows CI failures that still reproduce after the
recent codegen fixture and compatibility fixes.

This story is not a general Windows cleanup. It is a targeted follow-up for the
current failing contract and integration checks that are still red in the
native Windows `debug-system` preset.

## Problem

The recent Windows fixes resolved the earlier `fmt` public-include regression in
the codegen fixture helpers, and the following tests are now green again:

- `gentest_codegen_module_command_retargeting`
- `gentest_codegen_module_cache_resource_dir_isolation`
- `gentest_codegen_compile_command_macro_scan`

However, a broader native Windows run still exposes separate failure classes in
other parts of the matrix:

- `gentest_codegen_manifest_depfile_escaped_paths`
- `gentest_codegen_public_module_imports`
- `gentest_coverage_report_script_contract`
- `gentest_xmake_textual_consumer`
- `gentest_xmake_module_consumer`
- `gentest_xmake_xrepo_consumer`

These are not all the same bug. They represent different Windows portability
problems:

- invalid filename characters in generated escaped-path outputs
- deep-path / dyndep path handling for public-module scan-deps flows
- script launcher assumptions in CMake contract checks
- Xmake Windows path-depth, CRT, and fmt-link integration mismatches

## User stories

As a maintainer, I want Windows-only failures to be separated by real root cause
instead of treated as one blob, so fixes can stay small and defensible.

As a contributor, I want contract checks and external buildsystem smokes to use
Windows-valid paths and launchers, so CI failures reflect product bugs instead
of harness assumptions.

As a reviewer, I want Windows portability fixes to avoid widening behavior or
adding unrelated coverage work, so the branch only changes what the failing
lanes actually require.

## Current failure inventory

### 1. Escaped-path filename handling

Failing test:

- `gentest_codegen_manifest_depfile_escaped_paths`

Observed failure on Windows:

- a generated output filename contains `:`
- Windows rejects the path before codegen can replace/write the output file

Meaning:

- the escaped-path regression currently assumes a filename that is valid on
  POSIX but not on Windows
- this is a portability problem in the regression fixture or naming strategy,
  not in the earlier `fmt` helper fix

### 2. Public-module scan-deps pathing

Failing test:

- `gentest_codegen_public_module_imports`

Observed failure on Windows:

- the `consumer_auto_bad` subcase reaches a `clang-scan-deps` / dyndep step for
  generated mock `.cppm` inputs
- the `.ddi.tmp` write or rename step fails with `The system cannot find the
  path specified`

Meaning:

- this is a Windows generated-path / dyndep pathing issue in the public-module
  import regression
- it is distinct from the fixed `fmt/format.h` include-closure bug

### 3. Coverage script Windows entry behavior

Failing test:

- `gentest_coverage_report_script_contract`

Observed failure on Windows:

- the contract check runs `python3 scripts/coverage_report.py --help`
- the command fails even though CMake already discovers Python

Meaning:

- the contract test should use the interpreter CMake found, not assume a
  `python3` launcher exists in the environment

### 4. Xmake consumer integration on Windows

Failing tests:

- `gentest_xmake_textual_consumer`
- `gentest_xmake_module_consumer`
- `gentest_xmake_xrepo_consumer`

Observed failure classes on Windows:

- depfile/path-depth write failures in the textual and module consumer checks
- CRT / `_ITERATOR_DEBUG_LEVEL` mismatch in the xrepo consumer check
- unresolved `fmt::vprint` during the xrepo consumer link step

Meaning:

- the first two look like Windows path-depth and depfile-layout problems in the
  Xmake harnesses
- the xrepo case is a real ABI/link contract mismatch between the packaged
  runtime and the Xmake consumer configuration

## Scope

In scope:

- the six currently failing Windows checks listed above
- CMake regression scripts that drive those checks
- Windows-valid filename and path handling in those harnesses
- Windows interpreter/tool launcher resolution inside those checks
- Xmake consumer contract alignment for CRT / fmt linkage where the current
  Windows lane proves it is wrong

Out of scope:

- adding new Windows coverage work
- widening the Windows test matrix
- unrelated Linux/macOS cleanup
- the already-fixed Fedora LLVM/zstd compatibility issue
- the already-fixed `fmt` fixture include-closure issue

## Design direction

Treat the failures as four separate problems and keep the fixes narrow.

1. Normalize or avoid Windows-invalid filename characters in the escaped-path
   depfile regression.

2. Shorten or reshape generated Windows paths in the public-module import
   regression so `clang-scan-deps` dyndep outputs can be written reliably.

3. Replace hardcoded `python3` launcher assumptions with the discovered Python
   interpreter in the coverage-report contract check.

4. Split Xmake follow-ups by failure class:
   - path-depth / depfile write behavior for textual and module consumers
   - ABI / CRT / fmt-link contract mismatches for the xrepo consumer

## Rollout

1. Reproduce each failing Windows test individually.
2. Fix the coverage-script launcher issue first because it is the smallest,
   least coupled problem.
3. Fix the escaped-path depfile regression with a Windows-valid naming rule.
4. Triage and fix the public-module dyndep path issue.
5. Fix the Xmake path-depth failures.
6. Fix the Xmake xrepo ABI/link mismatch last, because it is the most coupled
   issue in the set.
7. Re-run the affected Windows tests after each change, then re-run the full
   native Windows `debug-system` preset.

## Acceptance criteria

- `gentest_coverage_report_script_contract` passes on native Windows by using
  the discovered interpreter rather than assuming `python3`.
- `gentest_codegen_manifest_depfile_escaped_paths` uses Windows-valid output
  names and passes.
- `gentest_codegen_public_module_imports` passes on native Windows without the
  earlier `fmt` include regression reappearing.
- `gentest_xmake_textual_consumer` and `gentest_xmake_module_consumer` pass on
  native Windows without depfile/path write failures.
- `gentest_xmake_xrepo_consumer` passes on native Windows with a consistent CRT
  / iterator-debug / fmt-link contract.
- the broader native Windows `ctest --preset=debug-system --output-on-failure`
  run no longer fails on these six tests.
