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

Devlog 2025-10-04 (Mock Generation)

  - Goals
      - Generate concrete mocks for interfaces and concrete types without hand-written glue.
      - Reuse clang discovery to find `gentest::mock<T>` instantiations directly in test sources.
      - Offer a fluent expectation API that integrates with the existing test context and failure reporting.
  - Key changes
      - Added `include/gentest/mock.h` with runtime helpers, expectation storage, and the public `gentest::expect` API.
      - Extended discovery to track `gentest::mock<T>` uses and gather method metadata (return types, qualifiers, overrides).
      - Generator now emits `mock_registry.hpp` (specializations + MockAccess glue) and `mock_impl.cpp` (method bodies,
        expectation dispatch) alongside each suite’s `test_impl.cpp`.
      - `gentest_attach_codegen()` wires the extra outputs automatically, defines `GENTEST_MOCK_REGISTRY_PATH`, and links the
        mock TU into the test target.
      - Introduced `tests/mocking` suite covering polymorphic overrides, non-virtual injection, and CRTP-style classes.
  - Status
      - Expectations support `times`, `returns`, `invokes`, and `allow_more`; missing/extra calls raise failures in the active
        test context.
      - Interfaces with virtual destructors override the destructor in the generated mock; non-virtual classes stay standalone.
      - Stub registry/impl files are still emitted when no mocks are present so includes remain stable.
  - Next ideas
      - Capture constructor metadata so mocks can forward to custom base constructors when a default isn’t available.
      - Support static functions and free-function mocking helpers.
      - Extend lint-only suite with additional mock diagnostics (final classes, private destructors, etc.).

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

Devlog 2025-10-03 (Review + Refactor Plan)

  Scope of review
  - Compared current HEAD against two commits back (3db383e – free-function fixtures). Since then we:
    - Implemented template param validation and later relaxed it to support interleaving.
    - Added many positive/negative tests for templates, NTTPs, boolean and string-like parameter axes.
    - Fixed string-pointer literal handling (const char*/wchar_t*/char8_t*/char16_t*/char32_t*), and extended detection for *_string_view types.
    - Removed expansion guardrails by design.

  Current state (progress)
  - Templates and parameters: robust expansion across types + NTTP + value axes, including interleaved declaration orders.
  - Fixtures: free-function fixtures (ephemeral) with optional setup/teardown; member fixtures unchanged and still supported.
  - CLI and emission: single TU generation with fmt-based templates; output format stable; tests assert counts via runners.
  - Validation: early, precise diagnostics for attribute misuse (unknown names, missing coverage, duplicates/conflicts).

  Hotspots and layering opportunities
  - discovery.cpp:run
    - Nested expansion loops and localized helpers make this function long and harder to evolve.
      - File: tools/src/discovery.cpp:228, 241, 245, 263 – nested for-chains for val/pack/template combos.
      - Suggestion: extract an AxisExpander utility that accepts a vector<vector<string>> and returns Cartesian products. Then compose:
        - template_arg_combos = expand_in_order(params_order, type_sets, nttp_sets)
        - value_arg_combos    = expand(parameter_sets)
        - pack_arg_combos     = expand(param_packs.rows)
        Each expansion becomes one call instead of hand-rolled loops.
    - Local string/char literal helpers duplicate logic we already centralized in type_kind.
      - File: tools/src/discovery.cpp:260-306 – is_string_type/is_char_type/string_prefix/is_string_literal/is_char_literal.
      - Suggestion: remove these lambdas and rely solely on classify_type/quote_for_type (tools/src/type_kind.cpp:38, 51).
    - Template validation is correct but lives inline in run().
      - File: tools/src/discovery.cpp:96-172 – validation + ordered expansion build.
      - Suggestion: factor into two helpers:
        - validate_template_attributes(FunctionDecl&, const AttributeSummary&)
        - build_template_arg_combos(const FunctionDecl&, const AttributeSummary&)
      - This reduces cyclomatic complexity of run() and keeps the flow readable.
    - Fixture type qualification via string concatenation can be brittle.
      - File: tools/src/discovery.cpp:191-214 – qualifies unqualified tokens with enclosing scope.
      - Suggestion: in a follow-up, prefer AST-derived fully-qualified parameter types from the function signature when fixtures(...) is used; use attribute tokens only for count/order. This eliminates namespace guessing.

  - render.cpp
    - render_wrappers still branches across four wrapper styles and constructs multiple strings inline.
      - File: tools/src/render.cpp:91-147.
      - Suggestion: introduce a small WrapperSpec { kind, callee, method, fixtures[], value_args } built earlier. Then a single switch drives template selection. Extract helpers:
        - build_fixture_decls(set)
        - build_fixture_setup_teardown(set)
        - build_call_args(fixtures, values)
      - Reduces local string assembly and makes new wrapper kinds (e.g., group subset runners) trivial.

  - type_kind.cpp
    - Recently added normalize_keep_ptr + broadened prefix detection are good. Two normalizers could be unified behind a parameter flag or small policy to avoid drift.
      - File: tools/src/type_kind.cpp:10, 24, 38, 51.
      - Suggestion: consolidate normalization and add explicit tests for char8_t* and std::u8string_view (we have u8string but not the view in tests yet).

  Testing gaps worth filling
  - Positive: interleaved triads mixing two NTTPs and a type with uneven value counts (already partially covered; add u8string_view and char8_t* axes).
  - Negative (lint-only): duplicate fixtures(...) tokens across multiple blocks; fixtures on member tests (already errors but add explicit smoke test).
  - Mixed axes: large but controlled matrices that mix multiple string encodings with booleans to keep quoting consistent.

  Proposed next refactors (no behavior change)
  1) Extract AxisExpander (header-only helper) and rewire discovery to use it for all Cartesian products.
  2) Move template attribute validation + ordered combo build into dedicated functions in discovery.cpp (or a small discovery_utils.hpp).
  3) Replace discovery’s local string/char helpers with type_kind exclusively; delete the lambdas.
  4) Introduce WrapperSpec and split render_wrappers into tiny helpers for declarations/setup/teardown/call assembly.
  5) Optional: plan the AST-based fixture signature adoption (keep behind a flag or apply only when fixtures(...) present).

  Risks and mitigations
  - Refactors touch code-gen plumbing. Keep PRs mechanical and gated by existing tests; expand template/fixture E2E tests first so behavior remains pinned.
  - Ensure no reliance on formatting in tests; continue using count/list checks.

  Status
  - All suites green (unit/integration/failing/skiponly/fixtures/templates). Interleaved templates + additional string/boolean axes covered. Lint-only negative tests surface clear diagnostics.

Devlog 2025-10-03 (WrapperSpec + Validation Toggle)

  - Wrapper emission polish
      - Added helper functions in render.cpp to generate fixture declarations, setup/teardown, and argument lists.
      - Introduced a lightweight WrapperSpec model (kind, wrapper_name, callee, method, fixtures, value_args) and a single switch-based renderer.
      - render_wrappers now builds a WrapperSpec per case and formats using the existing templates — no behavior change.

  - Template validation toggle
      - tools/CMakeLists.txt exposes GENTEST_ENABLE_TEMPLATE_VALIDATION (default ON).
      - When OFF, generator defines GENTEST_DISABLE_TEMPLATE_VALIDATION and skips template attribute checks, falling back to attribute-order expansion.

  - Tests and status
      - Rebuilt and ran ctest (debug preset): all tests still pass; no count changes.
      - Code is easier to read and extend (single source of truth for wrappers; validation can be toggled during debugging).
