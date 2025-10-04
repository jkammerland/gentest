Devlog 2025-10-02 (Refactor + Robust Tests)

  - Goals
      - Split generator into clear modules (discovery, parse, validate, emit, tooling)
      - Enforce strict attribute validation and editor diagnostics
      - Replace fragile golden tests with behavior-driven checks
      - Add skip-only coverage
  - Key changes
      - Generator refactor:
          - parse_core.hpp/.cpp: parse_attribute_list (no Clang deps)
          - parse.hpp/.cpp: source scanning for [[using gentest: …]], uses core parser
          - validate.hpp/.cpp: central rule enforcement (unknown gentest:: error; duplicates/conflicts error; value types checked)
          - discovery.hpp/.cpp: AST matchers + metadata extraction
          - emit.hpp/.cpp: template-based codegen only
          - tooling_support.hpp/.cpp: platform include dir discovery + arg helpers
          - main.cpp: minimal orchestrator; uses module interfaces
      - Lint-only mode:
          - gentest_codegen --check validates without output; CI tests for valid/invalid cases
      - Behavioral tests:
          - cmake/CheckTestCounts.cmake: counts “[ PASS ] / [ FAIL ] / [ SKIP ]”, asserts exit code semantics, and supports “--list” for discovery counts
          - count/list checks for unit, integration, failing (one failure), and skiponly (two skips)
      - Tidy plugin (optional):
          - gentest-attributes check, reuses the same rules as generator
          - README documents how to load it via -load into clang-tidy
  - Fixes and cleanup
      - Removed duplication of attribute rules and of parse_attribute_list; attr rules centralized in attr_rules.hpp; parsing centralized in parse_core.cpp.
      - Removed Fedora-specific include assumptions; generator augments includes generically where needed and otherwise relies on the compilation database.
      - Removed local pragma snippets from docs; INTERFACE target now exports warning suppressions so consumers don’t see “scoped attribute ignored”.
  - Status
      - Preset build/test runs clean across all suites and checks.
      - Unknown gentest:: attributes hard-fail; other namespaces warn.
      - INTERFACE warning flags propagate to consumers (Clang/GCC/MSVC).
      - New: Fixture support with member tests, including stateful fixtures and optional setup/teardown hooks.
        - Generator now detects class-level `[[using gentest: stateful_fixture]]`.
        - Member tests are grouped per fixture; optional `--shuffle-fixtures` shuffles within a group only.
        - Generated TU includes the test sources so fixture types are complete; tests no longer compile `cases.cpp` directly.
        - New header `include/gentest/fixture.h` exposes `gentest::FixtureSetup` and `gentest::FixtureTearDown` interfaces.
        - Added `tests/fixtures` suite with ephemeral and stateful examples; counts and `--list` checks added.
      - Output and emission cleanup:
        - Generated runtime switched from iostream to `fmt::print` to simplify output and reduce noise.
        - Emitter refactored to use block `fmt::format` constructions for wrappers, case entries, and group runners instead of many small appends.
  - Next ideas (optional)
      - Add a couple of core tests for skip(reason) at the parser/validator layer.
      - Minimize logic in parse.cpp further if trim_copy can be moved into a small shared utility (not necessary, just polish).
      - Expand tidy plugin docs with a minimal .clang-tidy example configuration for easy editor adoption.
      - Expose `--shuffle-fixtures` and `--seed` in README examples; consider environment fallback (e.g. `GENTEST_SHUFFLE`).
      - Validate and document fixture attribute semantics further (errors for class-scope attributes other than `stateful_fixture`).

  If you want, I can add the skip(reason) unit assertions and a tiny .clang-tidy example next.

Devlog 2025-10-03 (CLI + Templates + Phase 1 Plan)

  - CLI for VSCode
      - Implemented --help, --list-tests, --run-test, --filter alongside existing --list/--shuffle-fixtures/--seed
      - Selection mode uses ephemeral fixtures; full run preserves stateful fixtures
  - Template and parameter expansion
      - template(P, ...) across multiple blocks; Cartesian product
      - parameters(Type, ...) across multiple blocks; Cartesian product
      - parameters_pack((T0, T1, ...), (v0, v1), ...) bundles rows
      - String/char literal normalization and quoting; raw axis supported
  - Tests
      - templates suite exercises matrix, multi-axis, packs, raw, mixed templates+parameters
  - Docs
      - README templates section updated with quick examples and notes

  Phase 1 — Implementation plan and status
  - Type classification helper (to centralize literal rules)
      - Add type_kind.hpp/.cpp with `enum class TypeKind { String, Char, Integer, Floating, Enum, Raw, Other }`
      - `TypeKind classify_type(std::string_view)` strips whitespace/qualifiers and normalizes common forms
      - `std::string quote_for_type(TypeKind, std::string_view)` returns correctly quoted literal or verbatim
      - Status: planned (will wire into discovery literal emission)

  - NTTP in `template()`
      - Syntax: `template(NTTP: N, 1, 2, 3)` parsed into a new `template_nttp_sets` (order preserved)
      - Expansion: concatenate type tokens then NTTP tokens within `<...>` in the order of attributes
      - Status: planned; tests to add minimal `template<typename T, int N>` example (2 cases)

  - Guardrails for expansion explosion
      - Dropped by design: users may generate arbitrarily large matrices. No warnings/errors emitted for expansion size.

Devlog 2025-10-03 (Template/Parameter Validations)

  - Goal
      - Strengthen the templates suite to validate both template type instantiations and parameter axes, using `if constexpr` for type checks and exact value checks for parameters.
  - Changes
      - Enhanced existing cases with concrete assertions:
          - `templates/hello`: `if constexpr` verifies `T` is integral and `U` is floating.
          - `templates/params`: assert value ∈ {0,10,100}.
          - `templates/pairs`: assert cartesian pair membership across two axes.
          - `templates/strs`, `templates/bar`, `templates/pack`, `templates/raw`, `templates/chars`, `templates/wstrs`: added precise value checks.
      - New cases:
          - `templates/typed_values`: combines `template(T, int, long)` with `parameters(int, 2, 4)` and uses `if constexpr` to validate per-type.
          - `templates/nttp`: exercises NTTP via `template(NTTP: N, 1, 2)` with `template(T, int)` and validates `N`.
      - Counts updated:
          - Templates suite now has 29 cases; `tests/CMakeLists.txt` adjusted for counts and list-lines.
  - Notes
      - Build in this environment required `CCACHE_DISABLE=1` due to restricted tmp permissions; all tests pass under the debug preset.
  - Next
      - Add explicit NTTP example to README (done). Guardrails intentionally not implemented.

Devlog 2025-10-03 (Guardrails removed)

  - Decision
      - Removed expansion guardrails from `tools/src/discovery.cpp`. The generator will emit all requested combinations without diagnostics on size.
  - Docs
      - README updated to remove the guardrails section and note the intentional absence of limits.
  - Tests
      - No changes required; existing suites pass unchanged.

Devlog 2025-10-03 (Free-function Fixtures)

  - Feature
      - Added support for `[[using gentest: fixtures(A, B, C, ...)]]` on free functions.
      - The generated wrapper:
          - Default-constructs each fixture (ephemeral per test invocation).
          - Calls `gentest_maybe_setup(fx)` for each in declaration order.
          - Invokes the test with references to fixtures followed by any parameterized arguments.
          - Calls `gentest_maybe_teardown(fx)` in reverse order.
      - If a fixture derives from `gentest::FixtureSetup`/`FixtureTearDown`, hooks are invoked automatically.
      - Not supported on member tests; using `fixtures(...)` on a member function is a hard error.
  - Emission
      - New template partial `wrapper_free_fixtures` renders declarations/setup/call/teardown.
      - Type names are qualified with the enclosing function’s scope if unqualified in attributes.
  - Tests
      - Added `fixtures/free/basic` exercising A,B,C fixtures with setup/teardown validation.
      - Updated fixtures suite counts; all tests pass.
