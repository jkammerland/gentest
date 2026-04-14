# Story: Separate Generated-Code Devkit Boundary From Installed Runtime Detail

## Goal

Split the remaining installed generated-code/runtime compatibility surface from
the normal public `gentest` API so package consumers, public-module users, and
generated code each depend on the smallest contract they actually need.

This is a follow-up boundary-cleanup story, not a behavior change story.

## Problem

Story `023` already narrowed the normal `gentest/runner.h` path and the public
named module surface, but the repo still installs a broad runtime/detail layer
for generated code and legacy detail consumers:

- `include/gentest/fixture.h`
- `include/gentest/registry.h`
- `include/gentest/detail/fixture_runtime.h`
- `include/gentest/detail/registry_runtime.h`

That remaining layer is not ordinary public API anymore. It is a mixed
generated-code devkit / legacy-compatibility surface that still exposes:

- fixture allocation and ownership plumbing
- `FixtureHandle`
- case registration helpers like `register_cases(...)`
- shared-fixture runtime helpers and callback types

The current state is workable, but it has costs:

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

Treat the remaining installed detail layer as a devkit boundary problem.

Preferred shape:

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

1. inventory which generated templates still require:
   - `FixtureHandle`
   - `register_cases(...)`
   - `SharedFixtureScope`
   - shared-fixture typed lookup helpers
2. define the minimum unstable generated-code support contract needed by those
   templates.
3. switch generated templates and runtime-internal includes to that contract.
4. reduce or reclassify `gentest/fixture.h` / `gentest/registry.h` once the new
   devkit boundary is in place.
5. rerun the downstream/package/module matrix affected by generated code,
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

## Acceptance criteria

- Generated outputs depend on a smaller dedicated devkit contract instead of
  directly including or depending on the broad installed
  `detail/fixture_runtime.h` and `detail/registry_runtime.h` surface.
- Runtime-internal generated-code support call sites/includes are migrated off
  the same broad installed detail layer and onto that smaller dedicated devkit
  contract.
- `gentest/runner.h` remains narrow and does not regress by re-exposing the
  generated/runtime detail helpers removed during story `023`.
- `gentest/fixture.h` and `gentest/registry.h` are either:
  - reduced further, or
  - explicitly documented as legacy unstable compatibility shims with a smaller
    payload than today.
- `gentest_public_module_surface` and `gentest_public_module_detail_hidden`
  still pass, proving the public module export surface stays clean.
- `gentest_package_consumer_legacy_detail_contract` still passes so the
  remaining legacy compatibility-shim behavior is verified at the new
  boundary.
- `gentest_install_only_codegen_default`,
  `gentest_package_consumer_workdir_isolation`, and
  `gentest_package_consumer_executable_path`
  still pass so install/package contract details do not regress while the
  generated-code support boundary shrinks.
- Generated-code package and non-CMake downstream lanes still pass against the
  revised devkit boundary.

## Why Separate

Story `023` already achieved the high-value public-surface reduction:

- normal `gentest/runner.h` consumers no longer get the broad runtime-detail
  layer transitively
- the public module exports remain trimmed
- legacy detail consumers are covered explicitly through compatibility tests

What remains is a generated-code/devkit boundary cleanup with a different risk
profile. Keeping it separate makes the future package/module work easier to
reason about and avoids reopening the now-stable public-surface story.
