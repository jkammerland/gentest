# Story: Split `GentestCodegen.cmake` Into Focused Modules

## Goal

Reduce `cmake/GentestCodegen.cmake` from a ~3360-line universal integration file
into a thin public facade plus a set of focused internal CMake modules, so
codegen, module, mock, toolchain, and discovery behavior can evolve without all
living in one file.

This is a structure and maintainability story. It should not change the
supported user-facing `gentest_attach_codegen(...)`,
`gentest_add_mocks(...)`, or `gentest_discover_tests(...)` contracts.

## Problem

`cmake/GentestCodegen.cmake` currently mixes several mostly independent
subsystems:

- toolchain / host-tool / launcher argument construction
- source-inspector backend selection and orchestration
- explicit mock staging and mock-target plumbing
- TU-wrapper and manifest-mode output planning
- module scan-deps include/macro collection
- test discovery script generation and death-test helpers
- the public attach/add/discover entry points

That creates avoidable costs:

- reviews have to load unrelated context to evaluate a small change
- implementation risk is higher because helpers from different domains are
  interleaved in the same ~3360-line file
- line count alone makes ownership and boundaries unclear
- “move logic out of CMake” work is harder because there is no internal
  modular boundary to migrate from

## User stories

As a maintainer, I want `GentestCodegen.cmake` to be a thin facade, so changes
to scan-deps, explicit mocks, or test discovery do not all require editing the
same 3000-line file.

As a contributor, I want related helper functions to live together by concern,
so I can reason about one subsystem without mentally paging in the rest of the
repo’s build integration.

As a reviewer, I want refactors in `GentestCodegen.cmake` to be incremental and
mechanical, so structural cleanup is low-risk and easy to validate.

## Proposed file split

Keep `cmake/GentestCodegen.cmake` as the installed/public entry file, but make
it mostly:

- cache/config variable definitions
- `include(...)` calls for focused private modules
- thin facade wrappers or direct re-export from included modules for the public
  entry points

Preferred internal modules under `cmake/gentest/`:

- `CodegenToolchain.cmake`
  - host codegen target/executable resolution
  - common Clang argument collection
  - command launcher/env wrapping
  - codegen backend selection
- `CodegenInspector.cmake`
  - source-inspector executable discovery/probing
  - in-tree/native helper build path
  - source-inspector invocation and result extraction
- `ExplicitMocks.cmake`
  - explicit mock search-root expansion
  - staged-file materialization
  - imported explicit module mock mapping
  - mock-target link plumbing
- `TuMode.cmake`
  - wrapper/output naming helpers
  - wrapper property copying
  - TU-mode preparation and attachment
  - manifest-vs-wrapper shell attachment helpers
- `ScanDeps.cmake`
  - scan include-dir collection
  - scan macro argument collection
  - codegen dep-target traversal from the link graph
- `DiscoverTests.cmake`
  - discovery script generation
  - wildcard/regex helpers
  - death helper script materialization
  - `gentest_discover_tests_impl(...)`

## Scope

In scope:

- splitting `GentestCodegen.cmake` into focused CMake modules
- keeping the same public entry points and installed include path
- installing/exporting any new `cmake/gentest/*.cmake` helper modules needed by
  the installed package surface
- mechanically relocating helpers with behavior-preserving tests
- small helper renames needed to make internal boundaries clearer

Out of scope:

- redesigning codegen behavior
- removing supported output modes
- moving large amounts of logic from CMake into `gentest_codegen` itself
- unrelated package/runtime/module contract work already tracked by other
  stories

## First safe extraction step

Start with `DiscoverTests.cmake`.

Why first:

- the discovery helpers are already clustered near the end of the file
- they are comparatively self-contained
- they are less entangled with the attach/mocks/toolchain path than the other
  subsystems
- extracting them gives an immediate readability win without touching the most
  fragile codegen orchestration

First slice:

1. create `cmake/gentest/DiscoverTests.cmake`
2. move these helpers into it unchanged except for local naming cleanup:
   - `_gentest_write_discover_tests_script(...)`
   - `_gentest_add_command(...)`
   - `_gentest_generate_testname_guards(...)`
   - `_gentest_escape_square_brackets(...)`
   - `_gentest_wildcard_to_regex(...)`
   - `_gentest_ensure_check_death_script(...)`
   - `gentest_discover_tests_impl(...)`
   - `gentest_discover_tests(...)`
3. `include(...)` that module from `GentestCodegen.cmake`
4. keep the public function names unchanged, whether they are thin wrappers in
   `GentestCodegen.cmake` or direct re-exports from the included discovery
   module
5. validate with the discovery-heavy regression slice before moving on

## Rollout

1. Extract `DiscoverTests.cmake`.
2. Extract `CodegenInspector.cmake`.
3. Extract `TuMode.cmake`.
4. Extract `ExplicitMocks.cmake`.
5. Extract `ScanDeps.cmake`.
6. Extract `CodegenToolchain.cmake`.
7. Leave `GentestCodegen.cmake` as a thin facade with the public APIs and
   shared variable declarations.

That order is intentionally low-risk first, high-entanglement last.

## Acceptance criteria

- Status: **Done** on the current branch.
- Closure evidence:
  - `cmake/GentestCodegen.cmake` is a facade that declares configuration
    variables and includes focused modules from `cmake/gentest/`.
  - The helper modules are installed under `share/cmake/gentest/gentest/` so
    installed `find_package(gentest CONFIG REQUIRED)` consumers load the same
    facade graph as source-tree consumers.
  - Focused discovery, explicit mock, module-registration, textual wrapper,
    install-only codegen, and package-consumer tests passed after the split.

- `cmake/GentestCodegen.cmake` becomes a thin facade rather than the location
  of nearly all helper implementations.
- Story completion means all private helper implementations currently living in
  `GentestCodegen.cmake` have moved to focused modules under `cmake/gentest/`,
  leaving `GentestCodegen.cmake` responsible only for:
  - cache/config variable declarations
  - `include(...)` of the focused modules
  - public entry-point export/dispatch
- The internal helper implementations are split into focused modules under
  `cmake/gentest/` along the boundaries described above, or a demonstrably
  equivalent structure.
- The public entry points remain stable:
  - `gentest_attach_codegen(...)`
  - `gentest_add_mocks(...)`
  - `gentest_discover_tests(...)`
- The installed/exported package surface remains correct after the split:
  any new `cmake/gentest/*.cmake` modules required by `GentestCodegen.cmake`
  are installed and usable through `find_package(gentest CONFIG REQUIRED)`.
- Discovery-heavy and codegen-heavy regression slices remain green after each
  extraction step.
- The first extraction slice (`DiscoverTests.cmake`) lands with no behavior
  change and no caller-visible contract change.
- Structural cleanup makes future “move logic out of CMake” work easier by
  giving each subsystem an explicit home first.

## Validation target

At minimum, each extraction step should rerun the relevant focused slice:

- discovery-oriented tests after `DiscoverTests.cmake`
- mock/explicit-mock tests after `ExplicitMocks.cmake`
- TU-wrapper/manifest output tests after `TuMode.cmake`
- scan-deps/public-module tests after `ScanDeps.cmake`
- package/install/codegen-host-tool tests after `CodegenToolchain.cmake` and
  `CodegenInspector.cmake`
- installed package-consumer configure/build coverage whenever the include graph
  of `GentestCodegen.cmake` changes

## Why separate from story `025`

Story `025` was about moving source-inspection behavior out of the old CMake
parser/preprocessor path and into compiled code where appropriate.

This story is different:

- it is about internal modularization of the remaining CMake integration layer
- it is behavior-preserving structure work
- it prepares the ground for future CMake-to-tool migrations without bundling
  them into one large risky refactor
