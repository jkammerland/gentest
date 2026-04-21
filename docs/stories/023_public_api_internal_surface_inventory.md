# Public API Surface Inventory For Story 023

## Purpose

This file is the authoritative classification artifact for
`023_public_api_internal_surface_reduction.md`.

Use it to record which installed symbols or surfaces are:

- supported public API
- unstable detail API
- private implementation machinery that should not be installed

## Inventory

- `gentest::log`, `gentest::set_log_policy`, `gentest::set_default_log_policy`, `gentest::skip`,
  `gentest::skip_if`, `gentest::xfail`, `gentest::xfail_if`: `public` -> keep
- `gentest::ctx::current`, `gentest::ctx::Adopt`: `public` -> keep
- `gentest::ctx::Token`: `public` -> keep as the existing shared-pointer token type for compatibility; current
  reduction work hides the concrete `TestContextInfo` definition and removes the transitive `runtime_context.h`
  exposure from `context.h` without changing the token type
- `gentest/detail/runtime_support.h`: `detail` -> keep installed as the narrow unstable support layer needed by
  normal assertions/generated wrappers without re-exporting the concrete `TestContextInfo` layout
- `gentest/detail/runtime_context.h` and `gentest::detail::TestContextInfo`: `detail` -> still installed as an
  unstable escape hatch for in-repo/runtime users, but no longer exposed through the normal `runner.h` /
  `context.h` include path while the compatible token type remains
- `gentest/detail/registry_api.h`: `detail` -> keep installed as the narrow registry API layer included by
  `gentest/runner.h`; this is the new normal umbrella path for `run_all_tests`, `Case`, `FixtureLifetime`, and
  `registered_cases`
- `gentest/detail/case_api.h`: `detail` -> keep installed as the narrow case model layer used by `registry_api.h`
  and generated registration support; it exposes only `gentest::Case` and `gentest::FixtureLifetime`
- `gentest::run_all_tests`, `gentest::Case`, `gentest::FixtureLifetime`, `gentest::registered_cases`: `public`
  -> keep
- `gentest/detail/registration_runtime.h`: `detail` -> keep installed as the explicit unstable generated-code
  mutation layer for `gentest::detail::register_cases`; it is no longer included by the normal `runner.h` path
- `gentest/detail/registry_runtime.h`: `detail` -> keep installed temporarily as the direct legacy/runtime
  compatibility header for `snapshot_registered_cases`; generated code no longer includes it for registration
- `gentest::detail::register_cases`: `detail` -> keep installed as unstable generated-code surface, but no longer
  reachable from the normal `gentest/runner.h` umbrella path; available through `detail/registration_runtime.h` and
  `detail/generated_runtime.h`
- `gentest::detail::snapshot_registered_cases`: `detail` -> keep installed temporarily via
  `detail/registry_runtime.h`; no longer reachable from the normal `gentest/runner.h` umbrella path
- `gentest::detail::SharedFixtureScope`: `detail` -> keep temporarily while generated/runtime code still names it;
  no longer reachable from the normal `gentest/runner.h` umbrella path; available through
  `detail/generated_runtime.h`
- `gentest::detail::SharedFixtureCreateFn`, `gentest::detail::SharedFixturePhaseFn`: `detail` -> keep temporarily as
  the narrow unstable callback layer for shared-fixture registration via `detail/generated_runtime.h`
- `gentest::detail::register_shared_fixture(SharedFixtureScope, suite, fixture_name, create, setup, teardown)`:
  `detail` -> keep temporarily as the narrower unstable registration adapter via `detail/generated_runtime.h`
- `gentest::detail::SharedFixtureRegistration`: `private` -> removed from installed surface by the first story-023
  registry slice
- `gentest::detail::setup_shared_fixtures`, `gentest::detail::teardown_shared_fixtures`,
  `gentest::detail::get_shared_fixture`: `detail` -> `get_shared_fixture` is part of `detail/generated_runtime.h`;
  setup/teardown remain only in the direct legacy/runtime `detail/fixture_runtime.h` compatibility header
- `gentest::detail::register_shared_fixture<Fixture>`: `detail` -> keep temporarily as unstable generated-code
  helper via `detail/generated_runtime.h`
- `gentest::detail::get_shared_fixture_typed<Fixture>`: `detail` -> keep temporarily as unstable generated-code
  helper via `detail/generated_runtime.h`
- `gentest/detail/fixture_api.h`: `detail` -> keep installed as the narrow fixture API layer included by
  `gentest/runner.h`; this is the new normal umbrella path for `FixtureSetup` and `FixtureTearDown`
- `gentest::FixtureSetup`, `gentest::FixtureTearDown`: `public` -> keep
- `gentest/detail/generated_runtime.h`: `detail` -> keep installed as the explicit unstable generated-code devkit for
  fixture allocation, `FixtureHandle`, shared-fixture registration/lookup, and mutable case registration
- `gentest/detail/fixture_runtime.h`: `detail` -> keep installed temporarily as a direct legacy/runtime
  compatibility header for shared-fixture setup/teardown on top of `detail/generated_runtime.h`
- `gentest/fixture.h`: `detail` -> compatibility shim that keeps the historical include path working by exposing
  the narrow fixture API plus `detail/generated_runtime.h`; generated code should include the devkit directly
- `gentest/registry.h`: `detail` -> compatibility shim that keeps the historical include path working by exposing
  `detail/generated_runtime.h` plus `detail/registry_runtime.h`; generated code should include the devkit directly
- `gentest::detail::ErasedDeleter`, `gentest::detail::FixtureAllocation`, `gentest::detail::HasGentestAllocate*`,
  `gentest::detail::IsUniquePtr`, `gentest::detail::adopt_unique`, `gentest::detail::adopt_raw`,
  `gentest::detail::assign_allocation`, `gentest::detail::allocate_fixture*`, `gentest::detail::FixtureHandle`:
  `detail` -> generated-code devkit internals intentionally exposed through `detail/generated_runtime.h`; move to
  non-installed/private headers only if future generated output no longer needs them in installed consumer builds
