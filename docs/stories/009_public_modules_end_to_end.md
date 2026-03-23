# Story: Public Modules End-to-End

## Goal

Deliver a downstream `find_package(gentest)` workflow that can:

1. `import` the public gentest API from named modules
2. author tests in named module units
3. run gentest codegen against module-authored tests
4. support tests, benches, jitters, fixtures, and mocks where the mocked type is defined in a module

## Current Status

- [x] Public named module exports are packaged and consumable
- [x] Downstream package consumer uses public module imports in normal builds
- [x] `gentest_attach_codegen()` discovers named module units
- [x] Generated registration shims work for module-authored cases
- [x] Module-defined mocks work in named modules
- [x] Package consumer exercises test + bench + jitter + fixture + mock flows

## TDD Slice

The first red test is the downstream package consumer project under `tests/consumer/`.

That consumer is being converted from a manual header-based registration smoke test
into a real module-based scenario:

1. `main.cpp` imports the public gentest module
2. `cases.cppm` exports gentest cases from a named module interface
3. the consumer target calls `gentest_attach_codegen()`
4. CTest/package smoke runs:
   - default test run
   - explicit bench run
   - explicit jitter run
   - list output

## Notes

The biggest implementation risks are:

1. bridging `gentest_attach_codegen()` from text-included TUs to module-authored units
2. preserving target-local mock generation while exposing a module-only surface to normal builds

## Implemented Shape

- Public named modules now cover:
  - `import gentest;`
  - `import gentest.bench_util;`
  - `import gentest.mock;`
- TU wrapper mode now understands named module units and emits registration headers using explicit output paths instead of inferring from scanned source basenames.
- Module-defined mocks are supported by:
  - importing `gentest.mock` for the base API in normal builds
  - loading `gentest/mock.h` with `GENTEST_NO_AUTO_MOCK_INCLUDE` in the global module fragment only for the `GENTEST_CODEGEN` parse path
  - defining the mocked type in the named module
  - automatically injecting generated mock specializations into generated module wrapper sources after the mocked type definition
- Mixed targets are supported by partitioning generated mock outputs by visibility domain:
  - one header/global mock shard
  - one generated shard per named module
  - module-owned types are attached directly in generated module wrappers
  - header-defined mock types used from named modules get the generated header-domain mock code injected automatically

This removes the last user-facing include step from the named-module mock flow. Consumers now stay on `import gentest.mock;`, while the build-generated wrapper layer handles target-local specialization attachment internally.

## Verified

- `ctest --test-dir build/modules-clang -R '^(mocking|mocking_inventory|gentest_explicit_mock_target_surface|gentest_package_consumer)$' --output-on-failure`
