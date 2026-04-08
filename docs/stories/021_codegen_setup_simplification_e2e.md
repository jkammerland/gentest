# Story: Simplify Codegen Compatibility Setup And Core Test Support

## Goal

Reduce accidental complexity in the `tools/` CMake setup without weakening the
real compatibility contract that gentest still needs for LLVM/Clang packaging,
Terminfo, and cross-host core tests.

This story is intentionally not a "delete the compatibility layer" effort.
It is a narrowing and restructuring effort:

- keep the compatibility shims that are still paying for themselves
- remove dead or duplicated paths
- stop scraping private executable properties back into tests
- make the support-target graph match the real dependency tiers

## Problem

`tools/CMakeLists.txt` and `tools/core_tests/CMakeLists.txt` currently carry a
mix of necessary platform workarounds and accidental complexity.

The current pain points are:

- shared LLVM/codegen settings are partly modeled on `gentest_codegen`, then
  copied back into `gentest_core_*` tests
- support targets exist, but they do not fully encode the settings their
  consumers actually need
- some compatibility branches are redundant or unreachable
- the target graph is harder to reason about than the real dependency tiers
  justify

Recent fixes also confirmed that the current shape is fragile:

- Linux/core-test regressions appeared when LLVM-backed helper tests stopped
  inheriting settings indirectly
- cross and platform-specific lanes exposed hidden assumptions about local
  codegen target existence
- compatibility fixes had to be spread across targets and helper functions
  instead of flowing from one shared definition point

## User stories

As a maintainer, I want the codegen compatibility setup to have one obvious
source of truth for shared ABI/link settings, so platform fixes do not require
scraping private target properties.

As a contributor, I want `gentest_core_*` targets to express what they actually
depend on, so changing one executable does not silently break unrelated core
tests.

As a reviewer, I want compatibility workarounds to be easy to classify as
either necessary or dead, so future cleanup does not accidentally remove the
parts that still protect real CI lanes.

## Scope

In scope:

- `tools/CMakeLists.txt`
- `tools/core_tests/CMakeLists.txt`
- low-risk support-target recomposition in the local `tools/` tree
- removal of dead/redundant compatibility branches
- targeted validation on Linux and native secondary hosts where the affected
  paths are real

Out of scope:

- shrinking the full PR CI matrix
- removing the zstd alias shim
- removing the Terminfo target/shim concept
- removing the Windows `diaguids.lib` patch
- large discovery/parser refactors that would split new libraries out of
  `discovery.cpp`

## Design direction

Keep the existing compatibility surface, but simplify how it is expressed.

Planned direction:

1. Trim dead or redundant paths first.
   - skip extra LLVM prefix probing when explicit package roots are already
     provided
   - remove unreachable fallback logic

2. Recompose the support targets around real tiers.
   - headers / compile ABI settings
   - LLVM-light support
   - full Clang tooling support

3. Stop copying private properties from `gentest_codegen` into
   `gentest_core_discovery_tests` when the shared support layer can carry the
   required settings directly.

4. Keep executable-only runtime behavior explicit.
   - if a setting is truly executable-specific, model it as such
   - do not hide it behind a generic "copy all codegen settings" helper

## Experiment-backed constraints

The earlier simplification experiments already ruled out one tempting but bad
direction:

- removing `_gentest_copy_codegen_runtime_settings(...)` looked viable for
  `gentest_core_discovery_tests` on Linux
- flattening the support targets into direct raw library lists did not hold:
  it broke with missing LLVM transitive pieces such as
  `llvm::itaniumDemangle`

So this story explicitly does **not** target a raw-library flattening. The
right direction is better composition of the existing tiers, not pretending the
tiers are unnecessary.

## Rollout

1. Commit this story and work on a dedicated branch/worktree.
2. Apply low-risk cleanup in `tools/CMakeLists.txt`.
3. Rework `tools/core_tests/CMakeLists.txt` to consume shared support instead
   of scraping `gentest_codegen`.
4. Run targeted Linux validation for the affected core/codegen tests.
5. Run at least one native secondary-host validation pass if runtime/link
   behavior changes again.
6. Only keep the changes if they simplify the graph without reopening recent CI
   regressions.

## Acceptance criteria

- `_gentest_copy_codegen_runtime_settings(...)` is removed or reduced to a
  narrowly justified executable-only helper.
- `gentest_core_log_tests`, `gentest_core_discovery_utils_tests`, and
  `gentest_core_discovery_tests` consume explicit shared support rather than
  private executable state.
- dead/redundant compatibility branches are removed without changing supported
  behavior.
- Linux targeted validation passes for the affected `gentest_core_*` and
  codegen regression slices.
- native secondary-host validation is run when the refactor changes runtime or
  platform-sensitive linking behavior.
