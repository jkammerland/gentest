# PR Review TODO

Open follow-ups from the PR-style batch review against `origin/master..HEAD`.

These are intentionally deferred from the current CI-fix pass. The immediate
goal of this pass was to get the branch green again.

## High

- Install-only native builds should not require LLVM/Clang codegen by default.
  - Current behavior keeps `GENTEST_BUILD_CODEGEN=ON` whenever `gentest_INSTALL=ON`,
    even with `gentest_BUILD_TESTING=OFF`.
  - That pulls `tools/` and LLVM/Clang into package-only producer builds.
  - Relevant files:
    - `CMakeLists.txt`
    - `tools/CMakeLists.txt`

- Shared-runtime exports for public TLS context need to be audited and fixed.
  - `g_current_test` / `g_current_buffer` moved out of the header, but the
    declarations/definitions need a confirmed exported-shared-library contract.
  - Relevant files:
    - `include/gentest/detail/runtime_context.h`
    - `src/runtime_context.cpp`
    - `src/CMakeLists.txt`

- Long mock-domain shard filename derivation must be unified between CMake and
  `gentest_codegen`.
  - CMake and the generator currently use different shortening/hash logic for
    long domain labels.
  - Relevant files:
    - `cmake/GentestCodegen.cmake`
    - `tools/src/render_mocks.cpp`
    - `tools/src/main.cpp`

## Medium

- README named-module mock example needs to match the real working consumer pattern.
  - The docs still show an unconditional `import gentest` / `import gentest.mock`
    path, while the checked-in consumer fixture uses the required
    `#if !defined(GENTEST_CODEGEN)` guard.
  - Relevant files:
    - `README.md`
    - `tests/consumer/cases.cppm`

- Named-module precompile for `clang-cl` / MSVC-style compile databases needs a
  dedicated review and likely a Windows-specific command construction path.
  - Relevant files:
    - `tools/src/main.cpp`

- Package smoke coverage should include the common in-source build layout
  (`cmake -S . -B build` under the source tree), not only sibling source/build dirs.
  - Relevant files:
    - `tests/cmake/scripts/CheckPackageConsumer.cmake`
    - `tests/consumer/CMakeLists.txt`

