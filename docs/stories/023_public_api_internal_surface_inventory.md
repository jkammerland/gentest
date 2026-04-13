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
- `gentest::run_all_tests`, `gentest::Case`, `gentest::FixtureLifetime`, `gentest::registered_cases`: `public`
  -> keep
- `gentest::detail::register_cases`: `detail` -> keep installed as unstable generated-code surface
- `gentest::detail::snapshot_registered_cases`: `private` -> move toward private runtime header
- `gentest::detail::SharedFixtureScope`: `detail` -> keep temporarily while generated/runtime code still names it
- `gentest::detail::SharedFixtureRegistration`: `private` -> replace with narrower registration adapter
- `gentest::detail::register_shared_fixture(const SharedFixtureRegistration &)`: `private` -> remove from installed
  surface after tests/codegen stop constructing raw registration records
- `gentest::detail::setup_shared_fixtures`, `gentest::detail::teardown_shared_fixtures`,
  `gentest::detail::get_shared_fixture`: `private` -> move toward private runtime header
- `gentest::detail::register_shared_fixture<Fixture>`: `detail` -> keep temporarily as unstable generated-code
  helper until a narrower adapter exists
- `gentest::detail::get_shared_fixture_typed<Fixture>`: `detail` -> keep temporarily as unstable generated-code
  helper until generated wrappers stop depending on it
- `gentest::FixtureSetup`, `gentest::FixtureTearDown`: `public` -> keep
- `gentest::detail::ErasedDeleter`, `gentest::detail::FixtureAllocation`, `gentest::detail::HasGentestAllocate*`,
  `gentest::detail::IsUniquePtr`, `gentest::detail::adopt_unique`, `gentest::detail::adopt_raw`,
  `gentest::detail::assign_allocation`, `gentest::detail::allocate_fixture*`, `gentest::detail::FixtureHandle`:
  `detail` -> move to non-installed detail/private headers once runtime/codegen stop emitting them into installed
  consumer-facing code paths
