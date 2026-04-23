# Story: Reduce Installed Public API Leakage Of Runtime Internals

## Goal

Shrink the installed public `gentest` surface so consumers get a smaller, more
stable API and internal runtime plumbing can evolve without dragging package
compatibility behind it.

This is a deliberate surface-reduction story, not a cosmetic header shuffle.

## Problem

The installed headers currently expose internal runtime and fixture details that
look like implementation machinery rather than supported extension points.

Examples from the current review:

- shared-fixture registration protocols in `include/gentest/registry.h`
- `TestContextInfo` ownership and lifecycle surfaces in `include/gentest/context.h`
- fixture allocation plumbing and related templates in `include/gentest/fixture.h`

That has several costs:

- package consumers see internals as if they were supported API
- implementation refactors become source-compatibility risks
- large template-heavy headers increase review and compile noise
- runtime internals are harder to hide behind narrower contracts

## User stories

As a maintainer, I want runtime and fixture internals to stop leaking through
installed headers, so I can change implementation details without creating
accidental API commitments.

As a package consumer, I want the installed headers to present the supported
testing API rather than internal registration and context machinery, so it is
clear what is stable to depend on.

As a reviewer, I want public API changes and internal runtime refactors to be
separated cleanly, so compatibility risk is visible instead of buried in header
implementation details.

## Scope

In scope:

- public headers under `include/gentest/`
- installation surface and exported header layout
- runtime and fixture types that should move to private or explicitly unstable
  detail headers
- replacing exposed implementation structs with narrower adapters or opaque
  handles where appropriate

Out of scope:

- unrelated execution-model redesign
- changing supported test author APIs without a migration story
- codegen pipeline simplification outside the public header boundary

## Design direction

Treat the current public surface as an API inventory exercise first.

The authoritative classification artifact for this story should live in
`023_public_api_internal_surface_inventory.md`.

1. Classify exposed symbols into three groups:
   - supported public API
   - unstable `detail` API that should not be encouraged for downstream use
   - fully private runtime machinery that should not be installed

2. Move lifecycle and registration internals behind narrower entry points:
   - opaque handles where consumers do not need struct layout
   - non-template helpers where templates are only carrying internal ownership
     state
   - private runtime headers in `src/` or non-installed detail headers

3. Keep any remaining installed `detail` surface intentionally small and
   explicitly documented as unstable.

## Rollout

1. Inventory every installed symbol currently reachable from
   `include/gentest/{registry,context,fixture}.h`.
2. Record the public/detail/private classification in
   `023_public_api_internal_surface_inventory.md`.
3. Mark which surfaces are truly downstream-supported today.
4. Introduce private replacements before deleting or narrowing the old exposed
   types.
5. Preserve public behavior with compatibility shims only where the API is
   intentionally supported.
6. Re-run package and downstream smoke tests against the reduced install
   surface, including:
   - `gentest_runtime_shared_context_exports`
   - `gentest_public_module_surface`
   - `gentest_public_module_detail_hidden`
   - `gentest_meson_wrap_consumer`
   - `gentest_xmake_textual_consumer`
   - `gentest_xmake_module_consumer`
   - `gentest_xmake_xrepo_consumer`
   - `gentest_bazel_textual_consumer`
   - `gentest_bazel_module_consumer`
   - `gentest_bazel_bzlmod_consumer`
   - `gentest_package_consumer_workdir_isolation`
   - `gentest_package_consumer_executable_path`
7. When `GENTEST_ENABLE_PACKAGE_TESTS=ON`, also re-run:
   - `gentest_package_consumer_include_only`
   - `gentest_package_consumer_runtime_only_include_only`
   - `gentest_package_consumer_double_include_only`
   - `gentest_package_consumer`
   - `gentest_package_consumer_native_codegen`
   - `gentest_package_consumer_include_only_native_codegen`
   - `gentest_package_consumer_runtime_only`
   - `gentest_package_consumer_runtime_only_include_only_native_codegen`
   - `gentest_package_consumer_double`
   - `gentest_package_consumer_double_include_only_native_codegen`
   - `gentest_package_consumer_relwithdebinfo_exact_config`
   - `gentest_package_consumer_minsizerel_exact_config`
   - `gentest_package_consumer_gcc`

## Acceptance criteria

- `TestContextInfo` and comparable runtime-only ownership machinery are no
  longer exposed as part of the normal installed API shape unless they are
  intentionally documented as unstable detail.
- `023_public_api_internal_surface_inventory.md` exists and records the
  supported public, unstable detail, and private classifications for the
  installed surface under review.
- Shared-fixture registration internals are hidden behind narrower supported
  entry points or moved out of installed headers.
- `include/gentest/{registry,context,fixture}.h` no longer expose the same
  runtime ownership and registration internals they expose today unless those
  symbols are intentionally downgraded to unstable detail.
- `gentest_runtime_shared_context_exports` still passes against the installed
  surface.
- `gentest_public_module_surface` and `gentest_public_module_detail_hidden`
  still pass so public module exports do not start leaking new detail surface.
- `gentest_meson_wrap_consumer`, `gentest_xmake_textual_consumer`,
  `gentest_xmake_module_consumer`, `gentest_xmake_xrepo_consumer`,
  `gentest_bazel_textual_consumer`, `gentest_bazel_module_consumer`, and
  `gentest_bazel_bzlmod_consumer` still pass so the reduced install surface
  remains consumable outside the CMake package-only path.
- `gentest_package_consumer_workdir_isolation` and
  `gentest_package_consumer_executable_path`
  still pass so packaging contract details do not regress while headers shrink.
- When `GENTEST_ENABLE_PACKAGE_TESTS=ON`, the include-only package consumer
  checks
  `gentest_package_consumer_include_only`,
  `gentest_package_consumer_runtime_only_include_only`,
  `gentest_package_consumer_double_include_only`,
  `gentest_package_consumer_include_only_native_codegen`,
  `gentest_package_consumer_runtime_only_include_only_native_codegen`, and
  `gentest_package_consumer_double_include_only_native_codegen`
  still pass.
- When `GENTEST_ENABLE_PACKAGE_TESTS=ON`, the package consumer checks
  `gentest_package_consumer`,
  `gentest_package_consumer_native_codegen`,
  `gentest_package_consumer_runtime_only`,
  `gentest_package_consumer_double`,
  `gentest_package_consumer_relwithdebinfo_exact_config`, and
  `gentest_package_consumer_minsizerel_exact_config`
  still pass against the supported public API.
- When the GCC package-consumer lane is enabled,
  `gentest_package_consumer_gcc`
  still passes against the supported public API.

## Latest validation

First reduction slice on `2026-04-14` removed the installed raw shared-fixture
registration record without touching the wider `FixtureHandle` surface:

- `include/gentest/registry.h`
  - removed installed `gentest::detail::SharedFixtureRegistration`
  - replaced the raw-record runtime entry point with a narrower callback-form
    `register_shared_fixture(scope, suite, fixture_name, create, setup, teardown)`
- repo-internal shared-fixture regressions were rewritten to call the new
  callback-form API directly

Validation for that slice:

- targeted rebuild:
  `cmake --build --preset=debug-system --target gentest_regression_shared_fixture_reentry gentest_regression_shared_fixture_teardown_exit gentest_regression_fixture_group_shuffle_invariants gentest_regression_member_shared_fixture_setup_skip gentest_regression_member_shared_fixture_setup_skip_bench_jitter`
  -> passed
- shared-fixture regression band:
  `ctest --preset=debug-system --output-on-failure -R '^(regression_shared_fixture_.*|regression_member_shared_fixture_setup_skip.*|regression_fixture_group_shuffle_invariants)$'`
  -> `85/85` passed
- install/public-surface guards:
  `ctest --preset=debug-system --output-on-failure -R '^(gentest_public_module_surface|gentest_public_module_detail_hidden|gentest_runtime_shared_context_exports|gentest_install_only_codegen_default)$'`
  -> `4/4` passed
- minimal package-consumer smoke:
  `ctest --preset=debug-system --output-on-failure -R '^(gentest_package_consumer_include_only|gentest_package_consumer)$'`
  -> `2/2` passed

Second reduction slice on `2026-04-14` narrowed the normal runner include path
and made the remaining legacy detail surface explicit compatibility shims:

- `include/gentest/runner.h`
  - now includes only `assertions.h`, `context.h`,
    `detail/fixture_api.h`, and `detail/registry_api.h`
  - no longer exposes `gentest::detail::register_cases`,
    `gentest::detail::snapshot_registered_cases`,
    `gentest::detail::SharedFixtureScope`, or
    `gentest::detail::register_shared_fixture(...)` through the normal umbrella
    path
- `include/gentest/fixture.h`
  - now acts as a compatibility shim that layers
    `detail/fixture_runtime.h` on top of the narrow fixture API
- `include/gentest/registry.h`
  - now acts as a compatibility shim that layers
    `detail/registry_runtime.h` on top of the narrow registry API/runtime split
- generated/runtime code that still intentionally needs the unstable detail
  registration helpers now includes `gentest/detail/fixture_runtime.h` and/or
  `gentest/detail/registry_runtime.h` explicitly instead of receiving them
  transitively from `runner.h`
- installed package coverage now includes
  `gentest_package_consumer_legacy_detail_contract`, which proves the explicit
  `fixture.h` and `registry.h` compatibility shims still work for legacy detail
  consumers
- `023_public_api_internal_surface_inventory.md`
  - now records the public/detail/private classification for the installed
    `context.h`, `registry.h`, and `fixture.h` surface under review
  - explicitly classifies `gentest::CurrentToken` as the public token type
    while `gentest::detail::TestContextInfo` remains an installed unstable
    detail escape hatch that is no longer exposed through the normal
    `runner.h` / `context.h` include path

Validation for the current slice:

- rebuild of the exact current tree:
  `cmake --build --preset=debug-system`
  -> passed
- final exact-current-tree validation band:
  `ctest --preset=debug-system --output-on-failure -R '^(gentest_proof_api_context_adopt_copy_contract|gentest_proof_api_context_event_chronology|gentest_proof_api_context_always_log|gentest_proof_api_context_log_policy|gentest_proof_api_context_expect_throw_std_exception|gentest_proof_api_context_header_isolation|gentest_proof_api_assertions_header_isolation|gentest_runner_registry_runtime_hidden|regression_runtime_selection_duplicate_name_summary_first_location|regression_runtime_selection_duplicate_name_summary_second_location|regression_shared_fixture_.*|regression_member_shared_fixture_setup_skip.*|regression_fixture_group_shuffle_invariants|regression_measured_local_fixture_.*|regression_mg_lf_.*|gentest_public_module_detail_hidden|gentest_public_module_surface|gentest_runtime_shared_context_exports|gentest_meson_wrap_consumer|gentest_xmake_textual_consumer|gentest_xmake_module_consumer|gentest_xmake_xrepo_consumer|gentest_bazel_textual_consumer|gentest_bazel_module_consumer|gentest_bazel_bzlmod_consumer|gentest_package_consumer_workdir_isolation|gentest_package_consumer_executable_path|gentest_package_consumer_legacy_detail_contract|gentest_package_consumer|gentest_package_consumer_native_codegen|gentest_package_consumer_runtime_only|gentest_package_consumer_double|gentest_package_consumer_include_only|gentest_package_consumer_runtime_only_include_only|gentest_package_consumer_double_include_only|gentest_package_consumer_include_only_native_codegen|gentest_package_consumer_runtime_only_include_only_native_codegen|gentest_package_consumer_double_include_only_native_codegen|gentest_package_consumer_relwithdebinfo_exact_config|gentest_package_consumer_minsizerel_exact_config|gentest_package_consumer_gcc|gentest_codegen_template_option_removed)$'`
  -> `122/122` passed
- `clang-tidy` lane:
  `./scripts/check_clang_tidy.sh build/debug-system`
  -> passed
- batch review:
  `results/story023d_review_final6.md`
  -> no findings

This closes story `023` at the current simplified public-surface scope. The
normal `gentest/runner.h` path is now narrow, and the legacy detail surface is
called out explicitly through compatibility headers instead of leaking through
the normal umbrella path. The story inventory artifact is populated, and the
`context.h` / `TestContextInfo` classification question is resolved at the
current scope by preserving the compatible public token type while keeping the
concrete runtime context layout off the normal public include path.

The remaining generated-code/devkit compatibility cleanup is tracked
separately in `032_generated_runtime_devkit_boundary_cleanup.md`. That deferred
scope includes the generated-code/devkit install/package checks that still need
to move with the compatibility boundary, including future revalidation of
`gentest_install_only_codegen_default` when that boundary changes again.

Follow-up status: story `032` is now closed on the current branch. Generated
wrappers include `detail/registration_runtime.h` for registration-only output
or `detail/generated_runtime.h` for fixture/shared-fixture support. The legacy
`gentest/fixture.h` and `gentest/registry.h` compatibility shims now layer that
smaller generated devkit instead of making generated code include broad
fixture/registry runtime-detail headers directly.
