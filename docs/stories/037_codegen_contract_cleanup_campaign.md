# Story: Codegen Contract Cleanup Campaign

## Goal

Retire duplicated and legacy code paths that remain in the build-system wrappers
now that `gentest_codegen` owns the artifact manifest contract (stories `034` +
`035`). Define a single deprecation schedule with measurable removal targets and
version gates, so the wrappers shrink to thin manifest consumers rather than
parallel re-implementations of C++ semantics.

This is a subtraction story. It sequences the dependent closures of stories
`015` and `033` into one explicit demolition plan, and assigns owners and dates
to deprecations that previously lived only as footnotes in other story
closures. Story `032` remains adjacent installed-devkit work, not a `037`
closure gate. Story `036` was rejected, so declaration-only textual
registration is not a cleanup prerequisite.

## Problem

Story `034` closed with the note:

> broader legacy module-wrapper source-transformation removal remains in the
> cleanup campaign after wrapper-mode users migrate

Before this file, that campaign was not tracked as a story. As a result:

- Wave 1 removed the CMake configure-time source inspector module
  (`cmake/gentest/CodegenInspector.cmake`) and the scan include-dir / macro
  collectors that supported it. CMake no longer parses C++ to discover module
  names or `gentest.mock` imports before build time.
- `gentest_codegen` now emits explicit mock aggregate module interfaces with
  `--mock-aggregate-module-output` and `--mock-aggregate-module-name`, using
  the same source/module classification it already computes for wrapper and
  mock output planning. Installed consumers pass explicit module source
  candidates to the tool; the tool verifies the candidate's actual module
  declaration before using it.
- Legacy manifest mode (`OUTPUT=...` single-TU path) is removed on the `2.0.0`
  branch. The CMake option, CLI `--output`, legacy `--template`, and
  `NO_INCLUDE_SOURCES` / `GENTEST_NO_INCLUDE_SOURCES` paths hard-fail with
  replacement guidance. Direct codegen tests that used manifest output now use
  TU-mode registration headers.
- `xmake/gentest.lua` (1748 LOC) and `build_defs/gentest.bzl` (996 LOC) each
  reimplement scan-and-emit decisions in their host language instead of
  consuming the artifact manifest the tool already emits.
- `xmake/templates/` (6 `.in` files) and `meson/*.in` (4 files) duplicate
  skeletons that `gentest_codegen` is already capable of emitting directly.
- The legacy `share/cmake/gentest/scan_inspector/` install shape has package
  consumer absence coverage so stale helper packaging is caught.
- `EXPECT_SUBSTRING` alias for `DEATH_EXPECT_SUBSTRING` is removed on the
  `2.0.0` branch. `gentest_discover_tests(...)` now hard-fails when the
  top-level alias is used, while `EXTRA_ARGS EXPECT_SUBSTRING` still forwards a
  literal test argument.
- Until this story's first slice, there was no repo-level `DEPRECATIONS.md`;
  deprecation state was scattered across warning strings and story closure
  notes.

The stories that border this work each define part of the cleanup context:

| Story | Owns | Relationship to `037` |
| --- | --- | --- |
| `015` | non-CMake (Bazel/Xmake/Meson) parity | Lua/Bazel wrapper collapse, `.in` template removal |
| `032` | installed devkit boundary | adjacent installed API cleanup; does not gate `037` closure |
| `033` | `GentestCodegen.cmake` modularization | unblocked wave 1; configure-time source inspector and scan helper deletion is now landed in `037` |
| `036` | rejected declaration-only textual registration | no longer gates `037`; textual `.cpp` sources keep wrapper/include semantics |

Without a coordinating story, active dependencies can close individually while
the redundant wrapper code lives on indefinitely; rejected alternatives can also
keep appearing as false prerequisites.

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
  (`015`, `033`), while keeping adjacent `032` devkit work and rejected `036`
  context visible but out of the `037` closure gates.
- Drive removal of each deprecation at its scheduled version via explicit
  slices (warn -> hard-error -> delete).
- Add CI regression tests that assert each deprecation's warning fires in its
  warn window and that the removal version fails builds that still use the
  removed feature.
- Add package regression coverage that asserts the installed prefix does not
  contain the legacy `share/cmake/gentest/scan_inspector/` helper directory.

Out of scope:

- Designing new codegen protocols. Declaration-only textual registration was
  rejected in `036`; module registration and textual wrapper semantics are the
  active protocol paths.
- Adding non-CMake features to reach parity. Captured by `015`.
- Splitting `GentestCodegen.cmake` internals. Captured by `033`.
- Shrinking the installed runtime devkit surface. Captured by `032`.
- Changing the artifact manifest schema (`gentest.artifact_manifest.v1`,
  `gentest.mock_manifest.v1`). Schema freeze is a precondition.

## Deprecation inventory

The initial population of `DEPRECATIONS.md`:

| Feature | Replacement | Owning story |
| --- | --- | --- |
| `gentest_attach_codegen(OUTPUT=...)` legacy single-TU manifest mode | TU-wrapper mode (default) | `037` (removed in `2.0.0`) |
| `gentest_codegen --output <file>` and `--template <file>` | TU-wrapper mode with explicit per-input outputs | `037` (removed in `2.0.0`) |
| `NO_INCLUDE_SOURCES` option | not applicable once legacy manifest mode is removed | `037` (removed in `2.0.0`) |
| `EXPECT_SUBSTRING` argument to `gentest_discover_tests(...)` | `DEATH_EXPECT_SUBSTRING` | `037` (removed in `2.0.0`) |
| legacy `share/cmake/gentest/scan_inspector/` helper install shape | tool-owned source inspection; package absence guard | `037` (gated on `025` closure already done) |
| configure-time source inspector probe in CMake | tool-owned source/module classification, mock-manifest module metadata, codegen-emitted explicit mock aggregate modules, and manifest-declared textual wrapper semantics | `037` wave 1 (done) |
| CMake scan include-dir / macro collection helpers | tool-side `compile_commands.json` scan | `037` wave 1 (done) |
| `xmake/templates/*.in` and `meson/*.in` skeleton files | tool-emitted final sources | gated on `015` |
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
           (done)        (done)        (open)       (done)      (rejected)
           034           035           015          033          036
            \             \             \            \             :
             \             \             \            \            :
              \             \             \            \           :
               v             v             v            v          :
                             037  codegen contract cleanup campaign
```

Gates inside `037`:

- Kill wave 1 (CMake configure-time probe): done after `033`; preserves
  manifest-declared textual wrapper/include semantics and moves explicit mock
  aggregate module emission into `gentest_codegen`.
- Kill wave 2 (Lua/Bazel wrapper collapse + `.in` template removal): needs
  `015` non-CMake parity closure + manifest schema v1 frozen.
- Kill wave 3 (legacy manifest mode hard-remove): done on this `2.0.0`
  branch for CMake and direct CLI entry points.
- Kill wave 4 (`EXPECT_SUBSTRING` + `scan_inspector` install absence guard):
  package install absence coverage is done; `EXPECT_SUBSTRING` hard removal is
  done on this `2.0.0` branch.

## Rollout

1. Add `DEPRECATIONS.md` with the inventory table above and a short reader
   guide pointing to this story. **Done in the opening docs slice.**
2. Pin artifact manifest schemas (`gentest.artifact_manifest.v1`,
   `gentest.mock_manifest.v1`) with a freeze note in the docs.
3. Wave 4 (independent, cheap):
   - add package install coverage asserting
     `share/cmake/gentest/scan_inspector/` stays absent. **Done via the package
     consumer install smoke.**
   - document the `EXPECT_SUBSTRING` removal target in `DEPRECATIONS.md` and
     keep warning coverage in `gentest_discover_tests_smoke`. **Superseded by
     the `2.0.0` removal slice.**
   - after that target, remove `EXPECT_SUBSTRING` handling from
     `gentest_discover_tests(...)` and add a CI regression that asserts the
     hard-error behavior. **Done on the `2.0.0` branch.**
4. Wave 1 (after `033` closes): **Done.**
   - deleted `cmake/gentest/CodegenInspector.cmake` and removed its include
     from the CMake facade
   - deleted configure-time module/import extraction and scan macro/include-dir
     collectors from the CMake helper modules
   - moved explicit mock aggregate module emission into `gentest_codegen`
   - kept textual `.cpp` sources on manifest-declared wrapper/include mode;
     declaration-only textual registration remains rejected
5. Wave 2 (after `015` closes):
   - rewrite `xmake/gentest.lua` as a manifest consumer; target 600 LOC
   - rewrite `build_defs/gentest.bzl` the same way; target 400 LOC
   - delete `xmake/templates/*.in` and `meson/*.in`, emit final sources from
     the tool
6. Wave 3 (`2.0.0` legacy manifest removal branch):
   - remove legacy `OUTPUT=...` single-TU manifest mode from
     `gentest_attach_codegen(...)`. **Done.**
   - remove `gentest_codegen --output`, `--template`, and
     `NO_INCLUDE_SOURCES` / `GENTEST_NO_INCLUDE_SOURCES`. **Done.**
   - update examples and docs; add CI regression asserting hard-error behavior.
     **Done for the removed user-facing options.**
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

- Wave 1: `033`-owned module/source-inspection regressions, explicit mock
  target surface/install-export regressions, textual wrapper compatibility
  regressions, plus a package-consumer build that exercises a `.cppm` test
  source end-to-end without configure-time inspection.
- Wave 2: non-CMake consumer suites (`tests/downstream/meson_wrap_consumer`,
  `tests/downstream/xmake_xrepo_consumer`, Bazel bzlmod consumer),
  including module-variant coverage where parity landed in `015`.
- Wave 3: TU-wrapper direct codegen, depfile, mock include, module PCM cache,
  and CLI/CMake hard-error regressions for the removed legacy options.
- Wave 4: package-consumer install smoke scans the install tree and asserts
  `share/cmake/gentest/scan_inspector/` is absent; `EXPECT_SUBSTRING`
  hard-error coverage is part of `gentest_discover_tests_smoke`.

## Why separate from stories `015`, `032`, `033`, `036`

Those stories define the end-state of their respective areas. This story
owns the retirement schedule that ties their closures together:

- `015` adds features; `037` removes code those features make redundant.
- `032` shrinks the installed runtime devkit; `037` removes CMake and
  non-CMake scaffolding that only exists to support the older broader
  devkit surface.
- `033` modularizes `GentestCodegen.cmake`; `037` is the deletion pass that
  becomes mechanical once the modules exist.
- `036` records the rejected declaration-only textual alternative; `037`
  preserves textual wrapper/include mode instead of waiting for that rejected
  feature.

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
