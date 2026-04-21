# Story: Codegen Contract Cleanup Campaign

## Goal

Retire duplicated and legacy code paths that remain in the build-system wrappers
now that `gentest_codegen` owns the artifact manifest contract (stories `034` +
`035`). Define a single deprecation schedule with measurable removal targets and
version gates, so the wrappers shrink to thin manifest consumers rather than
parallel re-implementations of C++ semantics.

This is a subtraction story. It does not introduce new features or protocols.
It sequences the dependent closures of stories `015`, `032`, `033`, and `036`
into one explicit demolition plan, and assigns owners and dates to deprecations
that previously lived only as footnotes in other story closures.

## Problem

Story `034` closed with the note:

> broader legacy module-wrapper source-transformation removal remains in the
> cleanup campaign after wrapper-mode users migrate

Before this file, that campaign was not tracked as a story. As a result:

- `cmake/GentestCodegen.cmake` still ships ~500 lines of configure-time source
  inspection (`_gentest_probe_source_inspector_executable`,
  `_gentest_run_source_inspector`, `_gentest_extract_module_name`,
  `_gentest_file_imports_gentest_mock`, backend resolution) even though
  `gentest_codegen --inspect-source` and the artifact/mock manifests now expose
  these facts at the tool boundary.
- ~300 lines of scan include-dir and macro collection exist in CMake
  (`_gentest_collect_scan_include_dirs`, `_gentest_collect_scan_macro_args`,
  and their `_from_sequence` siblings) even though the tool already reads
  `compile_commands.json` for manifest validation scans.
- Legacy manifest mode (`OUTPUT=...` single-TU path) still compiles and runs
  with deprecation warnings, but no removal version is set. Its
  `NO_INCLUDE_SOURCES` escape hatch is deprecated with manifest mode and still
  needs its own warning coverage before removal.
- `xmake/gentest.lua` (1748 LOC) and `build_defs/gentest.bzl` (996 LOC) each
  reimplement scan-and-emit decisions in their host language instead of
  consuming the artifact manifest the tool already emits.
- `xmake/templates/` (6 `.in` files) and `meson/*.in` (4 files) duplicate
  skeletons that `gentest_codegen` is already capable of emitting directly.
- The legacy `share/cmake/gentest/scan_inspector/` install shape has no
  repo-level absence guard, so stale helper packaging can be reintroduced
  without a focused package regression catching it.
- `EXPECT_SUBSTRING` alias for `DEATH_EXPECT_SUBSTRING` warns but has no
  removal target.
- Until this story's first slice, there was no repo-level `DEPRECATIONS.md`;
  deprecation state was scattered across warning strings and story closure
  notes.

The open stories that border this work each own part of the cleanup but do not
coordinate it:

| Story | Owns | Blocks cleanup of |
| --- | --- | --- |
| `015` | non-CMake (Bazel/Xmake/Meson) parity | Lua/Bazel wrapper collapse, `.in` template removal |
| `032` | installed devkit boundary | compatibility shims in `gentest/fixture.h` / `gentest/registry.h` |
| `033` | `GentestCodegen.cmake` modularization | configure-time source inspector probe deletion, scan-deps collection deletion |
| `036` | declaration-only textual registration | removal of include-based `cases.cpp`-as-TU registration path |

Without a coordinating story, each of those can close individually while the
redundant wrapper code lives on indefinitely.

## User stories

As a maintainer, I want one document that lists every deprecated gentest
feature with a removal version, so contributors and users know when each
migration window closes.

As a wrapper maintainer (CMake / Xmake / Meson / Bazel), I want a single
artifact manifest contract to consume, so I can delete my local
C++-semantics reimplementation instead of keeping it in sync with the tool.

As a reviewer, I want every LOC-reduction claim in the campaign to have a
measurable target, so "cleanup" does not quietly become "reshuffle".

As a release manager, I want each deprecation removal to have CI regression
coverage, so the removal does not silently break a downstream consumer that
was tracking the warning window.

## Scope

In scope:

- Maintain `DEPRECATIONS.md` at the repo root with one row per deprecated
  feature or scheduled cleanup item, tracking: feature/item name, status,
  deprecated-since version, warn-since version where applicable,
  removal-target version or gate, replacement, owning story.
- Define LOC-reduction targets per wrapper and track them in this story's
  acceptance criteria.
- Define the dependency DAG between this campaign and the owning stories
  (`015`, `032`, `033`, `036`).
- Drive removal of each deprecation at its scheduled version via explicit
  slices (warn -> hard-error -> delete).
- Add CI regression tests that assert each deprecation's warning fires in its
  warn window and that the removal version fails builds that still use the
  removed feature.
- Add package regression coverage that asserts the installed prefix does not
  contain the legacy `share/cmake/gentest/scan_inspector/` helper directory.

Out of scope:

- Designing new codegen protocols. Captured by `036`.
- Adding non-CMake features to reach parity. Captured by `015`.
- Splitting `GentestCodegen.cmake` internals. Captured by `033`.
- Shrinking the installed runtime devkit surface. Captured by `032`.
- Changing the artifact manifest schema (`gentest.artifact_manifest.v1`,
  `gentest.mock_manifest.v1`). Schema freeze is a precondition.

## Deprecation inventory

The initial population of `DEPRECATIONS.md`:

| Feature | Replacement | Owning story |
| --- | --- | --- |
| `gentest_attach_codegen(OUTPUT=...)` legacy single-TU manifest mode | TU-wrapper mode (default) | `037` |
| `NO_INCLUDE_SOURCES` option | not applicable once legacy manifest mode is removed | `037` |
| `EXPECT_SUBSTRING` argument to `gentest_discover_tests(...)` | `DEATH_EXPECT_SUBSTRING` | `037` |
| legacy `share/cmake/gentest/scan_inspector/` helper install shape | tool-owned source inspection; package absence guard | `037` (gated on `025` closure already done) |
| configure-time source inspector probe in `GentestCodegen.cmake` | artifact manifest `module` fields, mock-manifest module metadata, and tool-side validation | gated on `033` + `036` |
| CMake scan include-dir / macro collection helpers | tool-side `compile_commands.json` scan | gated on `033` |
| `xmake/templates/*.in` and `meson/*.in` skeleton files | tool-emitted final sources | gated on `015` + `036` |
| `xmake/gentest.lua` scan / emit logic | manifest consumer + file staging only | gated on `015` |
| `build_defs/gentest.bzl` scan / emit logic | manifest consumer + file staging only | gated on `015` |

Each row in the table maps to one CI regression plus one deletion, hard-error,
or absence-guard slice in the rollout section below. `DEPRECATIONS.md` is now
the authoritative inventory; this story keeps the campaign rationale and
rollout gates.

## LOC reduction targets

Baseline as of the branch that opens this story:

| File / group | Baseline LOC | Target LOC | Delta |
| --- | --- | --- | --- |
| `cmake/GentestCodegen.cmake` | 3736 | 2200 | -1536 |
| `xmake/gentest.lua` | 1748 | 600 | -1148 |
| `build_defs/gentest.bzl` | 996 | 400 | -596 |
| `xmake/templates/*.in` | 6 files | 0 files | remove |
| `meson/*.in` | 4 files | 0 files | remove |

Total wrapper reduction target: **~3300 LOC** removed, across four buildsystem
integrations. The codegen tool is expected to grow by **~500-800 LOC** to
emit the manifest fields and final sources the wrappers stop computing; that
growth is explicitly accepted and does not count against the subtraction
target.

## Dependency DAG

```text
           (done)        (done)        (open)       (open)       (open)       (open)
           034           035           015          032          033          036
            \             \             \            \            \            /
             \             \             \            \            \          /
              \             \             \            \            \        /
               v             v             v            v            v      v
                             037  codegen contract cleanup campaign
```

Gates inside `037`:

- Kill wave 1 (CMake configure-time probe): needs `033` discoverable module
  split + `036` declaration-only textual registration closure.
- Kill wave 2 (Lua/Bazel wrapper collapse + `.in` template removal): needs
  `015` non-CMake parity closure + manifest schema v1 frozen.
- Kill wave 3 (legacy manifest mode hard-remove): needs one release cycle of
  warn coverage plus consumer migration evidence from `015`.
- Kill wave 4 (`EXPECT_SUBSTRING` + `scan_inspector` install absence guard):
  independent; the `DEPRECATIONS.md` and absence-guard slices can land
  immediately, while `EXPECT_SUBSTRING` hard removal waits for the documented
  removal target.

## Rollout

1. Add `DEPRECATIONS.md` with the inventory table above and a short reader
   guide pointing to this story. **Done in the opening docs slice.**
2. Pin artifact manifest schemas (`gentest.artifact_manifest.v1`,
   `gentest.mock_manifest.v1`) with a freeze note in the docs.
3. Wave 4 (independent, cheap):
   - add package install coverage asserting
     `share/cmake/gentest/scan_inspector/` stays absent
   - document the `EXPECT_SUBSTRING` removal target in `DEPRECATIONS.md`
   - after that target, remove `EXPECT_SUBSTRING` handling from
     `gentest_discover_tests(...)` and add a CI regression that asserts the
     hard-error behavior
4. Wave 1 (after `033` + `036` close):
   - delete `_gentest_probe_source_inspector_executable` and related
     configure-time probe helpers from `cmake/gentest/CodegenInspector.cmake`
     (post-`033` location)
   - delete configure-time `_gentest_file_imports_gentest_mock`,
     `_gentest_extract_module_name`, `_gentest_try_extract_module_name`
   - wire CMake consumers to read module classification from the artifact
     manifest and mock module coverage from the mock manifest or tool-side
     validation instead
5. Wave 2 (after `015` closes):
   - rewrite `xmake/gentest.lua` as a manifest consumer; target 600 LOC
   - rewrite `build_defs/gentest.bzl` the same way; target 400 LOC
   - delete `xmake/templates/*.in` and `meson/*.in`, emit final sources from
     the tool
6. Wave 3 (after one release cycle of warn coverage):
   - remove legacy `OUTPUT=...` single-TU manifest mode from
     `gentest_attach_codegen(...)`
   - remove `NO_INCLUDE_SOURCES` option
   - update examples and docs; add CI regression asserting hard-error behavior
7. Update `STATUS.md` to mark `037` `Done` when every row in
   `DEPRECATIONS.md` is either removed or has an assigned later story.

## Acceptance criteria

- `DEPRECATIONS.md` exists at the repo root, is linked from `README.md`,
  `docs/index.md`, and `docs/stories/STATUS.md`, and every deprecation in the
  initial inventory has a warn-since value or explicit N/A plus a removal
  target or gate.
- Artifact manifest schema freeze is documented and enforced by a CI
  contract check.
- Each kill wave lands behind the gates listed in the dependency DAG; no wave
  lands before its blocking story closes.
- The measured LOC reduction per wrapper meets or beats the targets in the
  LOC table. LOC is measured on the facade file plus any newly split
  modules, not just the single file that originally held the helper, so the
  campaign cannot succeed by splitting alone.
- Every deprecation removal has at least one CI regression test that asserts
  the feature is gone (hard-error for removed options, missing-file for
  removed install paths, missing-subcommand for removed wrapper calls).
- The public user-facing contracts remain stable throughout the campaign:
  - `gentest_attach_codegen(...)` (minus the removed legacy options)
  - `gentest_add_mocks(...)`
  - `gentest_discover_tests(...)` (minus `EXPECT_SUBSTRING`)
- Installed package consumers built against a release one major version
  before the removal can still configure and build with a warning, but the
  removal release makes them fail with a pointer to the replacement.

## Validation target

For each wave, run the relevant focused slice plus a package-consumer smoke
test:

- Wave 1: `033`- and `036`-owned module and textual regressions, plus a
  package-consumer build that exercises a `.cppm` test source end-to-end
  without configure-time inspection.
- Wave 2: non-CMake consumer suites (`tests/downstream/meson_wrap_consumer`,
  `tests/downstream/xmake_xrepo_consumer`, Bazel bzlmod consumer),
  including module-variant coverage where parity landed in `015`.
- Wave 3: full matrix of include-based and module-based consumers under both
  TU-wrapper mode and the removed legacy manifest mode, with CI asserting
  the hard-error message for the removed mode.
- Wave 4: discovery slice for `EXPECT_SUBSTRING` hard-error coverage and a
  package-consumer smoke test that scans the install tree and asserts
  `share/cmake/gentest/scan_inspector/` is absent.

## Why separate from stories `015`, `032`, `033`, `036`

Those stories define the end-state of their respective areas. This story
owns the retirement schedule that ties their closures together:

- `015` adds features; `037` removes code those features make redundant.
- `032` shrinks the installed runtime devkit; `037` removes CMake and
  non-CMake scaffolding that only exists to support the older broader
  devkit surface.
- `033` modularizes `GentestCodegen.cmake`; `037` is the deletion pass that
  becomes mechanical once the modules exist.
- `036` closes the last codegen emission gap for textual tests; `037` uses
  that closure to justify deleting the wrapper-side emission fallback.

Separating the campaign avoids each of those stories reopening after its own
closure just to delete residue, and gives the deprecation schedule one owner
and one home.

## Non-goals

- Changing the public CLI surface of `gentest_codegen` beyond what the
  removed wrapper features required.
- Rewriting the tests that cover removed features; those tests are either
  migrated to cover the replacement or removed with the feature they covered.
- Back-porting removals to older release branches. Removal targets are
  forward-only.
