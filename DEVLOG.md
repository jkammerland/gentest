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
      - New: Fixture support with member tests, including suite/global lifetimes and optional setup/teardown hooks.
        - Generator now detects class-level `[[using gentest: fixture(suite)]]` / `[[using gentest: fixture(global)]]`.
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
      - Validate and document fixture attribute semantics further (errors for class-scope attributes other than `fixture(...)`).

  If you want, I can add the skip(reason) unit assertions and a tiny .clang-tidy example next.

Devlog 2025-10-04 (Mock Generation)

  - Goals
      - Generate concrete mocks for interfaces and concrete types without hand-written glue.
      - Reuse clang discovery to find `gentest::mock<T>` instantiations directly in test sources.
      - Offer a fluent expectation API that integrates with the existing test context and failure reporting.
  - Key changes
      - Added `include/gentest/mock.h` with runtime helpers, expectation storage, and the public `gentest::expect` API.
      - Extended discovery to track `gentest::mock<T>` uses and gather method metadata (return types, qualifiers, overrides).
      - Generator now emits `mock_registry.hpp` (specializations + MockAccess glue) and `mock_impl.hpp` (inline method
        bodies, expectation dispatch) alongside each suite’s `test_impl.cpp`. The test TU includes `mock_impl.hpp` after
        including the test sources.
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

  - Value template parameters in `template()`
      - Syntax: `template(N, 1, 2, 3)` where `N` is a value template parameter; collected alongside type sets
      - Expansion: concatenate type/value tokens within `<...>` in declaration order
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
          - `templates/nttp`: exercises value template parameter via `template(N, 1, 2)` with `template(T, int)` and validates `N`.
      - Counts updated:
          - Templates suite now has 29 cases; `tests/CMakeLists.txt` adjusted for counts and list-lines.
  - Notes
      - Build in this environment required `CCACHE_DISABLE=1` due to restricted tmp permissions; all tests pass under the debug preset.
  - Next
      - Add explicit value template parameter example to README (done). Guardrails intentionally not implemented.

Devlog 2025-10-03 (Guardrails removed)

  - Decision
      - Removed expansion guardrails from `tools/src/discovery.cpp`. The generator will emit all requested combinations without diagnostics on size.
  - Docs
      - README updated to remove the guardrails section and note the intentional absence of limits.
  - Tests
      - No changes required; existing suites pass unchanged.

Devlog 2025-10-06 (Return Values in Tests)

  - Feature
      - Support non-void return values from test functions across free, free-with-fixtures, and member wrappers.
      - Generator now detects the test return type via Clang and emits either:
          - `[[maybe_unused]] const auto _ = <call>;` for non-void returns, or
          - `static_cast<void>(<call>);` for void returns.
        This avoids unused-result warnings while keeping behavior unchanged.
  - Emission
      - Added `returns_value` to `TestCaseInfo` (tools/src/model.hpp) and populate it in discovery (tools/src/discovery.cpp).
      - Updated wrapper templates in `tools/src/templates.hpp` to accept an `{invoke}` snippet.
      - Updated `render.cpp` to format `{invoke}` per-case (capture vs. void-cast) for all wrapper kinds.
      - Disabled forward declaration emission in `render_forward_decls` to avoid mismatches with non-void tests; sources
        are included before wrappers so prototypes are unnecessary.
  - Mocks usability
      - Generated inline mock implementations are now included via `GENTEST_MOCK_IMPL_PATH` from `gentest/mock.h`, so any
        TU in the test target can use `gentest::mock<T>` (even outside annotated `[[using gentest: test]]` functions).
        The registry header still provides class/MockAccess specializations via `GENTEST_MOCK_REGISTRY_PATH`.
  - Status
      - Behavior unchanged; only suppresses warnings and unblocks non-void tests. Existing suites compile and run as before.

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
    - Added many positive/negative tests for templates (types + value params), boolean and string-like parameter axes.
    - Fixed string-pointer literal handling (const char*/wchar_t*/char8_t*/char16_t*/char32_t*), and extended detection for *_string_view types.
    - Removed expansion guardrails by design.

  Current state (progress)
  - Templates and parameters: robust expansion across types + value template params + runtime axes, including interleaved declaration orders.
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
  - Positive: interleaved triads mixing two value template params and a type with uneven value counts (already partially covered; add u8string_view and char8_t* axes).
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

Devlog 2025-10-07 (Reporting Outputs)

  - Goals
      - Provide standard test reporting formats for CI consumption without altering existing console UX.
      - Keep the feature opt-in via CLI flags, with minimal runtime overhead when not enabled.
  - Key changes
      - JUnit XML: `--junit=<file>` writes a minimal JUnit report grouped by suite with per-test timing and CDATA failures.
      - GitHub Annotations: `--github-annotations` (or `GITHUB_ACTIONS=1`) emits `::error file=...,line=...,title=...::...` for each failure.
      - Allure results: `--allure-dir=<dir>` writes Allure 2 JSON results (status, timing, suite label, failure details) into the directory; created if missing.
  - Tests
      - Added `junit_smoke_unit` to verify the generated XML contains the expected suite element.
      - Added `github_annotations_smoke` to assert `::error` lines appear for a known failing test.
      - Added `allure_smoke_single` to verify a selected test produces a `passed` result JSON.
  - Notes
      - No behavior change without flags; color output and console formatting remain.
      - Help text documents new flags; README gains a Reporting section with examples.

Devlog 2025-10-07 (Allure Attachments + CI Artifacts)

  - Goal
      - Enrich Allure output with useful attachments and add CI-friendly checks for artifacts without affecting console UX.
  - Changes
      - Allure logs attachment: when a test fails and has per-test logs (via `gentest::log_on_fail(true)` / `gentest::log(...)`), the runner writes a text attachment and references it from the result JSON.
      - GitHub annotations retained from prior work; combined with logs this yields actionable CI output and post-fail artifacts.
      - JUnit unchanged; documented a pattern for creating a JUnit file per-suite.
  - Tests
      - Added `failing/logging/attachment` case that enables `log_on_fail` and logs two lines.
      - `allure_logs_attachment`: verifies Allure JSON references a logs attachment.
      - `allure_logs_attachment_file`: verifies the logs file content.
      - `junit_artifact_example`: smoke-checks JUnit file creation for the integration suite.
      - Updated `failing_counts` from 2 → 3 to include the new failing case.
  - CMake helpers
      - `cmake/RunAndCheckFile.cmake`: runs a program, does not assert on exit code, and then checks an artifact file for a substring (useful when generating artifacts on failure).
      - `cmake/CheckFileContains.cmake`: retained for zero-exit flows.
  - Status
      - All tests passing with new smoke checks via the debug preset.
      - No behavior change without the new flags; reporting remains opt-in.

Status
  - All suites green (unit/integration/failing/skiponly/fixtures/templates). Interleaved templates + additional string/boolean axes covered. Lint-only negative tests surface clear diagnostics.

Devlog 2025-10-06 (Matcher Helpers)

  - Goals
      - Add ergonomic argument matchers for mocks atop existing where_args.
      - Provide a concise alias and expand mocking tests, including a negative predicate check.
  - Key changes
      - New helpers in header-only API (include/gentest/mock.h):
          - gentest::match::Any() – accepts any value.
          - gentest::match::Eq(x) – equality match via operator==.
          - gentest::match::InRange(lo, hi) – inclusive range match via <=.
          - gentest::match::Not(p) – negates another matcher/predicate.
      - Added ExpectationHandle::where(...) as a convenience alias for where_args(...).
      - Tests (tests/mocking/cases.cpp): added three positive cases exercising Eq/Any, InRange, and Not.
      - Tests (tests/failing/cases.cpp): added a dedicated predicate_mismatch negative to validate failure reporting.
      - Updated test count checks for mocking (9 → 12) and failing (1 → 2).
  - Notes
      - Matchers are implemented as small generic lambdas; they integrate seamlessly with the existing std::function capture in set_predicates.
      - Mocks are now included as an inline header per test target; no separate mock TU is compiled. GENTEST_BUILDING_MOCKS
        guards are no longer necessary in test sources.
  - Status
      - All presets green locally (debug). Counts reflect the new cases.
      - Next: consider convenience wrappers (AnyOf/AllOf) and a whole-call predicate, if desired.
      - Changed mock emission to inline header (`mock_impl.hpp`) included by the generated test TU; removed the need for
        GENTEST_BUILDING_MOCKS guards in tests.

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

Devlog 2025-10-06 (Branch Merge: maintainance → mocking-support → more-expects → suites)

  - Scope
      - Merged the following branches into `master` in order: `maintainance`, `mocking-support`, `more-expects`, `suites`.
      - Saved backtrack tag: `pre-suites-merge` on `master` prior to merging `suites`.

  - Key conflict resolutions
      - `tools/src/discovery.cpp` (mocking-support merge): favored AxisExpander-style helpers for value/pack combos introduced earlier, preserving behavior while simplifying loops.
      - `AGENTS.md` (suites merge): kept system LLVM/Clang 20+ preset bullets and preserved existing regeneration/sanitizer guidance.
      - `tests/failing/cases.cpp` (suites merge): adopted suite-namespace form and removed the mocking-specific mismatch test from the root failing suite (it now belongs under mocking-support).
      - `tools/src/discovery.hpp|parse.cpp|emit.cpp` (suites merge): adopted suite-aware parsing and grouping (ephemeral/suite/global) and integrated accessors/runners wiring expected by `suites`.

  - Result
      - All tests pass on `master` after full merge: 35/35 with suite-aware grouping enabled.
      - Root failing expectations adjusted to 1 failure (mismatch test moved to mocking-support suite). The dedicated death test for the mismatch in root was removed accordingly.

  - Mocking-support history cleanup
      - Reworded three “Amend me” commits with descriptive messages:
          - Add argument matching (`.with(...)`), initial template-member support, `sys-llvm20` preset, and tests/docs.
          - Discovery AxisExpander helpers for value/pack combos; simplify `discovery.cpp`.
          - Add `where_args(...)` per-argument predicate matchers and tests.
      - No content changes, only commit message hygiene for clearer history.

  - Next
      - Consider adding ergonomic matchers (e.g., `Any()`, `Eq(x)`, `InRange(a,b)`) atop `where_args(...)` for convenience.
      - Continue small refactors in `render.cpp` to reduce inline assembly and keep templates localized.

Devlog 2025-10-06 (Mocks: Parameter Matching Plan)

  Context and recent cleanup
  - Simplified lifecycle: ExpectationHandle no longer verifies in its destructor; verification runs from generated `mock<T>::~mock()`
    via `InstanceState::verify_all()`. `InstanceState` destructor is defaulted to avoid double-verification.
  - Kept a single registry and dispatch path keyed by `MethodIdentity` to preserve overload/ref-qualifier/noexcept distinctions.
  - Safer default for reference returns without an action: terminate instead of UB; value returns still default to `R{}`.

  Goals (Parameter Matching)
  - Allow users to assert that specific argument values were passed to mocked methods, in addition to call counts.
  - Keep semantics simple and predictable; preserve existing FIFO expectation ordering.
  - Provide a minimal API now; leave room to extend with predicates/matchers later.

  Proposed API (Phase 1)
  - `ExpectationHandle::with(Args... expected)`
    - Captures a tuple of expected values by value (decayed). Applies to that expectation for all consumed calls.
    - Works with both value- and void-returning methods. Compatible with `times`, `returns`, `invokes`, and `allow_more`.

  Semantics (Phase 1)
  - Dispatch always considers the front expectation (unchanged FIFO behavior).
  - If `.with(...)` was set, compare actual args against the stored expected values using `==` per position.
    - On mismatch: record a failure (argument mismatch) in the active test context; still count the call against that expectation.
    - When `observed_calls == expected_calls` and `!allow_more`, pop the expectation (unchanged).
  - Rationale: avoids reordering or searching the queue; behavior remains ordered and deterministic.

  Error messages
  - Include method name and argument index; for printable types, show `expected` and `actual`. For non-streamable types, show type names only.
  - Example: `argument[1] mismatch for ::mocking::Calculator::compute: expected 30, got 12`.

  Implementation sketch
  - Runtime changes (header-only):
    - Extend `detail::mocking::Expectation<R(Args...)>` and `Expectation<void(Args...)>` with an optional matcher:
      - Store either `std::tuple<std::decay_t<Args>...>` for equality matching, or a `std::function<bool(const Args&...)>` if we add Phase 2.
      - Add a small `check_args(method_name, args...)` that records a mismatch failure when present.
    - `ExpectationHandle::with(Args&&...)` assigns the tuple on the underlying shared expectation and returns `*this`.
    - In `InstanceState::dispatch`, after retrieving the front expectation and before invoking, call `expectation->check_args(...)` when configured.
  - No generator changes required; registry and method identity stay the same.

  Tests (Phase 1)
  - Positive (mocking suite):
    - `mocking/interface/returns_matches`: `EXPECT_CALL(mock, &Calculator::compute).with(12,30).returns(42);` then `compute(12,30)` ⇒ pass.
    - `mocking/concrete/invokes_matches`: `EXPECT_CALL(mock, &Ticker::tick).times(3).with(1).invokes(...);` call `tick(1)` three times ⇒ pass.
    - `mocking/crtp/bridge_matches`: `EXPECT_CALL(mock, &DerivedRunner::handle).with(7).times(2);` call `handle(7)` twice ⇒ pass.
  - Negative (failing suite):
    - `mocking_args/mismatch`: mismatch in one or more positions ⇒ one recorded failure; place under `tests/failing` to keep suite counts stable.
  - Note: Adding these will update test counts for the involved suites; adjust `ctest` count checks accordingly.

  Future work (Phase 2+)
  - `.where(predicate)` accepting `bool(const Args&...)` for custom checks.
  - Simple matchers: `Eq(x)`, `Any()`, `InRange(a,b)`, etc., with `.with(Eq(...), Any(), ...)`.
  - Optional unordered matching flag to search the queue for the first matching expectation (trade off simplicity for flexibility).

  Risks and mitigations
  - Argument printing for non-streamable types: fall back to type names to avoid compile errors.
  - ABI/headers: remains header-only; no new source units. Keep changes minimal and documented.
  - Backward compatibility: existing tests that don’t call `.with(...)` are unaffected.

  Acceptance criteria
  - New `.with(...)` compiles and behaves as documented across GCC/Clang/MSVC.
  - New tests pass; mismatches produce a single, clear failure per call.
  - No regressions in existing mocking, fixtures, unit, integration, and templates suites.

Devlog 2025-10-06 (Mocks: Arg Matching Delivered + Template Members)

  - Implemented Phase 1 argument matching
      - Runtime now supports `ExpectationHandle::with(args...)` to match positional arguments via `==`.
      - Mismatch reporting includes index, fully qualified method, and expected/actual values (with type-name fallback when not streamable).
      - FIFO expectation semantics preserved: mismatches still consume the front expectation.

  - Tests
      - Positive: existing mocking cases extended with `..._returns_matches`, `..._invokes_matches`, and CRTP match examples.
      - Negative: added failing case that asserts the mismatch message content for `Calculator::compute`.
      - Updated mocking counts (now 8) to account for the new template-member test below.

  - Non-virtual template member methods (initial support)
      - Discovery records function templates and preserves parameter names; signatures for dependent types are printed as written.
      - Codegen:
          - Emits inline template member bodies in the generated registry header so templates are visible to call sites.
          - Keeps non-template member methods as out-of-line definitions in the mock implementation TU.
          - `MockAccess` gained a generic fallback that deduces the signature from the provided method pointer, enabling expectations for template specializations without enumerating all instantiations.
      - Test added: `mocking/concrete/template_member_expect_int` for `Ticker::tadd<T>`.

  - Deferred (documented; not implemented in this drop)
      - Value template member methods: intentionally skipped pending a broader refactor of template handling in discovery/emission.
        - Rationale: avoid duplicating logic before the planned AxisExpander/validation split and unified type/expr rendering.
        - Plan: cover `template<int N>` and mixed cv-ref/noexcept variants after refactor; extend tests then.

  - Toolchain preset (system LLVM 20+)
      - Added a `sys-llvm20` CMake preset that uses `/usr/bin/clang-20` and enforces LLVM >= 20.
      - Generators prefer `llvm-config-20 --cmakedir` when `GENTEST_REQUIRE_LLVM20=ON` to avoid accidentally picking older configs.

Devlog 2025-10-07 (Libassert Integration + No-Exceptions Semantics)

  - Goals
      - Integrate libassert with gentest while retaining our runner and output.
      - Define framework-wide behavior when exceptions are disabled (terminate on fatal, log clearly).
      - Provide presets and a death test to guard the behavior in CI.
  - Key changes
      - Adapter: `include/gentest/assert_libassert.h`
          - Installs a libassert failure handler that records into gentest’s active test context.
          - Adds `EXPECT/EXPECT_EQ/EXPECT_NE` via a thread-local non-fatal guard; `ASSERT_EQ/ASSERT_NE` helpers.
          - Fatal: throw `gentest::assertion` when exceptions are enabled; otherwise delegate to gentest’s terminate helper.
      - Core: `include/gentest/runner.h`
          - Detects `GENTEST_EXCEPTIONS_ENABLED`; introduces `terminate_no_exceptions_fatal(origin)`.
          - `require/require_eq/require_ne` log and `std::terminate()` with a clear message when exceptions are disabled.
      - Tests & CMake
          - Optional suite `tests/libassert/cases.cpp` (+ counts) under `GENTEST_WITH_LIBASSERT`.
          - No-exceptions target via `GENTEST_WITH_LIBASSERT_NOEXCEPT_TESTS`; uses `-fno-exceptions` or `/EHs-c-`, defines `_HAS_EXCEPTIONS=0` and `FMT_EXCEPTIONS=0`.
          - Death test asserts non-zero exit and the generic gentest message.
      - Presets
          - Added `libassert-noexc` configure/build/test/workflow presets.
  - Status
      - Existing suites unchanged; libassert suite and no-exc death test pass locally.
  - Next
      - Optionally pin libassert to a release tag; add throw-match adapters for exceptions-on builds; document vcpkg path.
Devlog 2025-10-10 (Mocks: Include Order, Diagnostics, EXPECT_CALL)

  - Goals
      - Make mock<T> usage symmetric and frictionless for virtual and non-virtual types.
      - Enforce robust include order in generated code so types are always visible where required.
      - Surface precise, actionable generator diagnostics for common failure modes.

  - Key changes
      - Generated TU include order: the generated test implementation now includes project sources first and then `gentest/mock.h`, ensuring the mock registry and inline method definitions are seen only after all original types are visible.
      - Generator diagnostics:
          - Incomplete target: improved message explaining virtual vs. non-virtual requirements (virtual must be complete; non-virtual tolerates forward-decls but parameter/return types must be visible).
          - Polymorphic-in-CPP: new detection that errors when a virtual interface’s definition is in a `.cpp`, with a message to move it to a header included before the registry.
      - Registry tolerance (non-virtual): emit forward declarations for referenced user types in their namespaces, so helper TUs that include `gentest/mock.h` early can still compile for non-virtual mocks.
      - Convenience macro: added `EXPECT_CALL(mock, method)` (and `ASSERT_CALL`) that routes to `gentest::expect` using the original interface type via `__gentest_target`, fixing pointer identity and making call sites concise.

  - Tests
      - Added generator negative checks under `tests/mock_errors`:
          - `incomplete_virtual_ref.cpp`: forward-declared target only → expect “target type is incomplete …”.
          - `virtual_defined_in_cpp.cpp`: polymorphic type defined in a `.cpp` → expect “polymorphic target appears defined in a source file …”.
      - CTest now runs these via `CheckDeath.cmake` using `gentest_codegen --check`; both pass.
      - All existing suites (unit, integration, fixtures, templates, mocking, failing) pass with the new include order and macros.

  - Impact
      - Virtual mocks: require the interface header to be included before the registry; the generated TU guarantees this by design.
      - Non-virtual mocks: incomplete `T` is fine for the specialization; forward-decls in the registry further reduce order sensitivity across TUs.
      - Helper usage: `gentest/mock.h` remains usable from any TU in the test target; now safer by default.
      - No extra translation units introduced; inline impl remains header-only to stay ODR-safe.

  - Files
      - Template: tools/src/templates.hpp
      - Discovery: tools/src/mock_discovery.cpp
      - Registry emission: tools/src/render_mocks.cpp
      - Runtime: include/gentest/mock.h (EXPECT_CALL)
      - Tests: tests/mock_errors/*, tests/CMakeLists.txt

  - Status
      - 100% tests passed locally with debug-system preset (48/48), including new negative generator checks.
      - This unblocks a clean, symmetric API surface for mocks with clear failure modes.
