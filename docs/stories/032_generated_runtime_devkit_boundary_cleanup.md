# Story: Separate Generated-Code Devkit Boundary From Installed Runtime Detail

## Goal

Split the remaining installed generated-code/runtime compatibility surface from
the normal public `gentest` API so package consumers, public-module users, and
generated code each depend on the smallest contract they actually need.

This is a follow-up boundary-cleanup story, not a behavior change story.

## Status

Done.

The current branch closes the story at the generated-code boundary level:

- public `Case` / `FixtureLifetime` moved to
  `include/gentest/detail/case_api.h`
- mutable case registration stays in
  `include/gentest/detail/registration_runtime.h`
- fixture binding, shared-fixture registration/lookup, and generated case
  registration are collected in the explicit unstable generated-code devkit
  `include/gentest/detail/generated_runtime.h`
- generated full-support wrappers include `generated_runtime.h`; lightweight
  registration wrappers include only `registration_runtime.h`
- generated-output smoke tests now assert those include boundaries directly
  and reject the broad compatibility headers in emitted artifacts
- `gentest/fixture.h` and `gentest/registry.h` remain installed legacy
  compatibility shims, but now layer the smaller generated devkit instead of
  broad fixture/registry runtime-detail headers
- `detail/fixture_runtime.h` is now only the direct legacy/runtime header for
  shared-fixture setup/teardown entry points on top of the generated devkit
- `detail/registry_runtime.h` is now only the direct legacy/runtime header for
  registry snapshots; it no longer provides mutable case registration
- package and shared-runtime smoke coverage includes the direct installed detail
  headers so the compatibility shims and the direct legacy headers are both
  covered

## Problem

Story `023` already narrowed the normal `gentest/runner.h` path and the public
named module surface, but before this story the repo still installed a broad
runtime/detail layer for generated code and legacy detail consumers:

- `include/gentest/fixture.h`
- `include/gentest/registry.h`
- `include/gentest/detail/fixture_runtime.h`
- `include/gentest/detail/registry_runtime.h`

That remaining layer was not ordinary public API anymore. It was a mixed
generated-code devkit / legacy-compatibility surface that still exposed:

- fixture allocation and ownership plumbing
- `FixtureHandle`
- case registration helpers like `register_cases(...)`
- shared-fixture runtime helpers and callback types

That state was workable, but it had costs:

- installed detail surface is larger than the actual public authoring API
- generated-code contracts are coupled to broad runtime headers
- package compatibility work and public-API cleanup stay entangled
- future module/package changes have to reason about a fuzzy devkit boundary

## User stories

As a maintainer, I want generated wrappers to depend on one explicit unstable
devkit contract instead of broad installed runtime headers, so internal fixture
and registry plumbing can keep moving without pretending to be public API.

As a package consumer, I want `gentest/runner.h` and `import gentest;` to stay
small and stable while any generated-code-only detail surface is clearly marked
as such.

As a reviewer, I want public API cleanup to be separate from generated-code
support cleanup, so compatibility risk and downstream impact are obvious.

## Scope

In scope:

- generated test wrapper/runtime support contract emitted by `gentest_codegen`
- installed detail headers used only for generated/runtime integration
- `include/gentest/{fixture,registry}.h` compatibility shims
- package and non-CMake downstream tests that exercise generated outputs
- public-module and textual consumer verification for this contract split

Out of scope:

- redesigning fixture semantics or allocation behavior
- changing the public author-facing `gentest` test API
- unrelated codegen model or rendering refactors already covered by other
  stories

## Design direction

Treat the remaining installed detail layer as a devkit boundary problem. The
implemented shape is:

1. `case_api.h` carries the stable public case model used by `runner.h`.
2. `registration_runtime.h` carries mutable generated-code case registration.
3. `generated_runtime.h` carries the generated fixture/shared-fixture binding
   contract and includes `registration_runtime.h`.
4. `fixture_runtime.h`, `registry_runtime.h`, `fixture.h`, and `registry.h`
   are compatibility layers rather than generated-code include targets.

Original preferred shape:

1. introduce one smaller explicit generated-code support contract
   - for example a dedicated unstable header for generated fixture binding,
     shared-fixture lookup, and case registration
   - keep it intentionally smaller than `fixture_runtime.h` +
     `registry_runtime.h`

2. switch emitted templates and runtime-internal call sites to that contract
   instead of broad direct dependence on:
   - `FixtureHandle`
   - allocation trait plumbing
   - broad shared-fixture helper families
   - raw registry mutation helpers

3. only after generated code is off the broader layer:
   - shrink `gentest/fixture.h` / `gentest/registry.h` further
   - or leave them as clearly documented legacy-compatibility shims with a much
     smaller implementation payload

4. keep the public named module stable
   - `import gentest;` should not gain new detail exports
   - the public module should remain validated separately from any textual
     generated-code support layer

## Rollout

Completed rollout:

1. inventoryed which generated templates still require:
   - `FixtureHandle`
   - `register_cases(...)`
   - `SharedFixtureScope`
   - shared-fixture typed lookup helpers
2. defined the minimum unstable generated-code support contract needed by those
   templates.
3. switched generated templates and generated-like hand-written regressions to
   `generated_runtime.h` or `registration_runtime.h`.
4. reduced `gentest/fixture.h` / `gentest/registry.h` to explicit legacy
   compatibility shims over the new boundary.
5. reran the downstream/package/module matrix affected by generated code,
   including:
   - `gentest_install_only_codegen_default`
   - `gentest_public_module_surface`
   - `gentest_public_module_detail_hidden`
   - `gentest_runtime_shared_context_exports`
   - `gentest_package_consumer_legacy_detail_contract`
   - `gentest_package_consumer_workdir_isolation`
   - `gentest_package_consumer_executable_path`
   - `gentest_package_consumer`
   - `gentest_package_consumer_native_codegen`
   - `gentest_package_consumer_runtime_only`
   - `gentest_package_consumer_double`
   - `gentest_package_consumer_include_only`
   - `gentest_package_consumer_runtime_only_include_only`
   - `gentest_package_consumer_double_include_only`
   - `gentest_package_consumer_include_only_native_codegen`
   - `gentest_package_consumer_runtime_only_include_only_native_codegen`
   - `gentest_package_consumer_double_include_only_native_codegen`
   - `gentest_package_consumer_relwithdebinfo_exact_config`
   - `gentest_package_consumer_minsizerel_exact_config`
   - `gentest_package_consumer_gcc`
   - `gentest_meson_wrap_consumer`
   - `gentest_xmake_textual_consumer`
   - `gentest_xmake_module_consumer`
   - `gentest_xmake_xrepo_consumer`
   - `gentest_bazel_textual_consumer`
   - `gentest_bazel_module_consumer`
   - `gentest_bazel_bzlmod_consumer`
   These are the registered CTest lanes for the local toolchain; non-CMake
   lanes keep their existing tool-availability skip behavior.

## Acceptance criteria

- Done: generated outputs depend on a smaller dedicated devkit contract instead
  of directly including or depending on the broad installed
  `detail/fixture_runtime.h` and `detail/registry_runtime.h` surface.
- Done: runtime-internal generated-code support call sites/includes are migrated
  off the same broad installed detail layer and onto that smaller dedicated
  devkit contract.
- Done: `gentest/runner.h` remains narrow and does not regress by re-exposing
  the generated/runtime detail helpers removed during story `023`.
- Done: `gentest/fixture.h` and `gentest/registry.h` are reduced legacy
  compatibility shims with a smaller payload than before this story.
- Done: `gentest_public_module_surface` and `gentest_public_module_detail_hidden`
  still pass, proving the public module export surface stays clean. The public
  module surface test now also builds negative import probes for
  `register_cases`, `snapshot_registered_cases`, `SharedFixtureScope`, and
  `register_shared_fixture`, and checks module-registration generated artifacts
  for the narrow include boundary.
- Done: `gentest_runtime_shared_context_exports` links against the shared
  runtime while taking the direct `detail/fixture_runtime.h` setup/teardown
  entry-point addresses, so those compatibility exports stay visible in shared
  builds.
- Done: `gentest_package_consumer_legacy_detail_contract` still passes so the
  remaining legacy compatibility-shim behavior is verified at the new
  boundary. The contract now covers both top-level legacy shims and direct
  installed detail includes for `generated_runtime.h`, `registration_runtime.h`,
  `fixture_runtime.h`, and `registry_runtime.h`.
- Done: `gentest_install_only_codegen_default`,
  `gentest_package_consumer_workdir_isolation`, and
  `gentest_package_consumer_executable_path`
  still pass so install/package contract details do not regress while the
  generated-code support boundary shrinks.
- Done: generated-code package and non-CMake downstream CTest lanes do not fail
  against the revised devkit boundary under their existing tool-availability
  policies.

## Why Separate

Story `023` already achieved the high-value public-surface reduction:

- normal `gentest/runner.h` consumers no longer get the broad runtime-detail
  layer transitively
- the public module exports remain trimmed
- legacy detail consumers are covered explicitly through compatibility tests

This generated-code/devkit boundary cleanup had a different risk profile from
story `023`. Keeping it separate made the package/module work easier to reason
about without reopening the already-stable public-surface story.
