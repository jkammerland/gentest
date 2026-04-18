# Story: Codegen-Owned Artifact Manifest and Same-Module Registration

## Goal

Make `gentest_codegen` the only layer that understands C++ source semantics for
test discovery, module registration, and mock extraction. Build systems should
only compose generated artifacts that `gentest_codegen` declares.

The immediate module goal is to replace module-source replacement / include-shim
registration with generated same-module implementation units:

```cpp
module;

#include "gentest/runner.h"
#include "gentest/fixture.h"

module my.tests;

// generated wrappers, case table, and static registrar
```

This keeps authored module interfaces in the normal build graph, avoids
`#include "cases.cppm"` hacks, and does not require users to export test
functions or fixture types just so registration code can call them.

Modules and textual sources share the artifact-manifest boundary, not the same
generated C++ shape. Named-module tests use additive same-module implementation
units. Textual tests use manifest-declared wrapper/include artifacts by default
because that is the only compatibility-preserving way to keep `.cpp`-local
declarations, anonymous namespaces, static functions, and fixture definitions
visible to generated registration code.

## Problem

Current and proposed shortcuts keep pushing C++ decisions into CMake:

- source-shape classification during configure
- module-name extraction before the build tool has compiled anything
- import detection from stripped source text
- partial compile-definition and generator-expression evaluation
- output planning that differs between CMake and non-CMake backends

That violates the intended boundary. CMake, Meson, Bazel, and Xmake should not
need to parse C++ or emulate the compiler. They should receive an artifact plan
from `gentest_codegen` and wire those artifacts into their native build graph.

## User Stories

As a module consumer, I want my authored `FILE_SET CXX_MODULES` sources to stay
as normal module sources, so my compile database and editor view match the code
I wrote.

As a build-system maintainer, I want `gentest_codegen` to emit a manifest of
artifacts, so CMake, Meson, Bazel, and Xmake can all wire the same outputs
without duplicating C++ source analysis.

As a maintainer, I want mock extraction and mock emission to be separate
codegen phases, so mock discovery can be used independently from test
registration and future build-system integrations do not need one monolithic
command mode.

## Target Model

Split codegen into explicit phases:

```text
gentest_codegen inspect
  inputs:
    - source list
    - compile database or explicit compile commands
    - build-system-declared output roots or concrete output paths
  outputs:
    - artifact manifest
    - depfile for discovered scan inputs

gentest_codegen emit
  inputs:
    - artifact manifest
  outputs:
    - textual wrapper/registration sources
    - same-module registration implementation units
    - depfiles for generated outputs
```

The first implementation must avoid a graph cycle: most build systems need the
generated output paths before the build graph is finalized, but `inspect` often
needs compile-command data produced by that graph. Therefore output paths are
predeclared by the build-system adapter and passed to `gentest_codegen`; the
manifest validates and classifies those concrete outputs instead of inventing
new paths that the build system did not know about.

This does not contradict story `029`: build systems may still choose concrete
roots or output paths for graph construction, while `gentest_codegen` owns the
semantic artifact roles, module/source classification, and manifest validation.

The manifest is the build-system contract. A minimal module entry should look
conceptually like:

```json
{
  "sources": [
    {
      "source": "tests/cases.cppm",
      "kind": "module-primary-interface",
      "module": "my.tests",
      "partition": null,
      "compile_context_id": "my_tests:tests/cases.cppm",
      "registration_output": "build/gentest/tu_0000_cases.registration.gentest.cpp"
    }
  ],
  "artifacts": [
    {
      "path": "build/gentest/tu_0000_cases.registration.gentest.cpp",
      "role": "registration",
      "compile_as": "cxx-module-implementation",
      "module": "my.tests",
      "owner_source": "tests/cases.cppm",
      "compile_context_id": "my_tests:tests/cases.cppm",
      "requires_module_scan": true,
      "generated_include_dirs": ["build/gentest"]
    }
  ]
}
```

The exact schema can evolve, but it must be sufficient for any supported build
system to add generated outputs to the correct target without parsing source
files itself.

At minimum, the manifest must represent:

- stable output path
- artifact role
- owning source
- source unit kind, including primary interface vs partition
- module name and partition name when applicable
- compile treatment, such as textual wrapper or same-module implementation unit
- target attachment mode
- whether the artifact includes and replaces the owning source
- compile-context identity or explicit compile command identity
- required generated include roots
- generated header dependencies
- depfile path
- whether native module scanning is required

## Module Example

Authored source:

```cpp
export module my.tests;

import gentest;
import gentest.bench_util;

namespace my_tests {

struct Fixture : gentest::FixtureSetup {
    void setUp() override { value = 7; }
    int value = 0;
};

[[using gentest: test("module/case")]]
void case_a(Fixture& fixture) {
    gentest::asserts::EXPECT_EQ(fixture.value, 7);
}

[[using gentest: bench("module/bench"), baseline]]
void bench_a(Fixture& fixture) {
    gentest::doNotOptimizeAway(fixture.value);
}

} // namespace my_tests
```

Generated same-module implementation unit:

```cpp
module;

#include <array>
#include <span>

#include "gentest/runner.h"
#include "gentest/fixture.h"

module my.tests;

namespace gentest::generated::tu_0000 {

static void case_a_wrapper(void* ctx) {
    auto& fixture = *static_cast<my_tests::Fixture*>(ctx);
    my_tests::case_a(fixture);
}

constexpr std::array<::gentest::Case, 1> kCases = {
    // generated metadata for module/case
};

struct registrar {
    registrar() {
        ::gentest::detail::register_cases(std::span{kCases});
    }
};

[[maybe_unused]] const registrar instance;

} // namespace gentest::generated::tu_0000
```

The user does not need to export `Fixture` or `case_a`. The generated unit is
part of the same named module, so it can reference declarations from the module
interface as module-internal implementation code.

## Textual Example

Textual test sources follow the same artifact-plan model, but they do not use
the same generated C++ strategy as modules. For textual `.cpp` sources,
wrapper/include mode is the default compatibility model. The build-system
simplification is that the wrapper becomes a declared codegen artifact described
by the manifest, not that textual tests stop using same-translation-unit
inclusion.

Authored source:

```cpp
#include "gentest/attributes.h"
#include "gentest/runner.h"

namespace {

struct LocalFixture : gentest::FixtureSetup {
    int value = 0;
    void setUp() override { value = 7; }
};

[[using gentest: test("textual/local")]]
static void local_case(LocalFixture& fixture) {
    gentest::asserts::EXPECT_EQ(fixture.value, 7);
}

} // namespace

namespace textual_tests {

[[using gentest: test("textual/case")]]
void case_a() {}

} // namespace textual_tests
```

Generated compatibility wrapper source:

```cpp
// generated wrapper
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "cases.cpp"

#if !defined(GENTEST_CODEGEN) && __has_include("tu_0000_cases.gentest.h")
#include "tu_0000_cases.gentest.h"
#endif
```

The generated header included by the wrapper contains wrappers, case tables, and
the static registrar. Because that header is compiled in the same translation
unit after the owning `.cpp`, it can reference source-local tests, fixture
types, `static` functions, anonymous namespaces, benchmarks, jitters, and mock
uses without forcing users to move declarations into headers.

The corresponding manifest entry should describe the wrapper semantics rather
than hiding them in a build-system-specific helper:

```json
{
  "artifacts": [
    {
      "path": "build/gentest/tu_0000_cases.gentest.cpp",
      "role": "registration",
      "compile_as": "cxx-textual-wrapper",
      "owner_source": "tests/cases.cpp",
      "includes_owner_source": true,
      "replaces_owner_source": true,
      "requires_module_scan": false
    }
  ]
}
```

Textual declaration-only registration is a possible future opt-in mode, not the
default. It can only work for sources that make test functions and all required
fixture/signature types visible to another translation unit:

```cpp
namespace textual_tests {
void case_a();
}

// generated standalone registration can call textual_tests::case_a(),
// but it cannot call anonymous-namespace or static tests in cases.cpp.
```

This stricter mode should have a separate source-style contract and diagnostics.
It must not become the default for existing textual tests.

## Build-System Contract

Build systems may:

- choose input sources
- choose output directories
- invoke `gentest_codegen inspect`
- read the artifact manifest
- add generated artifacts to native targets
- attach generated depfiles
- set native module-scanning properties on generated module implementation units

Build systems must not:

- parse C++ source text for module declarations or imports
- evaluate preprocessor conditions
- emulate compile-definition inheritance
- infer module names from file names
- decide mock extraction semantics
- replace authored module interface units as a registration mechanism

For textual sources, replacing the owning source with a generated wrapper is
allowed only when the codegen manifest declares textual wrapper compile
treatment and `replaces_owner_source: true`. That replacement is a textual
compatibility mechanism, not a build-system guess.

CMake wiring should become a thin composition layer, for example:

```cmake
gentest_attach_codegen(my_tests
  MODULE_REGISTRATION
  FILE_SET module_cases)
```

Internally that should mean:

```text
1. predeclare deterministic generated output paths from selected sources
2. add those generated sources to my_tests as private generated sources
3. run inspect/emit to validate and materialize the predeclared outputs
4. fail if the manifest does not match the predeclared outputs
5. let CMake/Ninja scan and compile the generated module implementation units
```

For CMake specifically, generated same-module implementation units should be
private generated sources with module scanning enabled. They must not replace
the authored `FILE_SET CXX_MODULES` entries. The authored `.cppm` remains in the
target and in `compile_commands.json`.

For Bazel, a build-time JSON manifest cannot declare new outputs during
analysis. The first Bazel path must predeclare outputs from rule attributes and
use the manifest as a validation/product file, or introduce a Starlark-visible
planning layer before actions are declared.

For Xmake, generated outputs should likewise be predeclared before build
commands run, then overwritten and manifest-validated by `gentest_codegen`.

For Meson, the first slice should stay textual-only unless a separate Meson
module build story is added. Meson is still useful to prove declared textual
outputs and depfile behavior.

## Mock Phase Split

Mock handling should be decomposed into standalone phases:

```text
gentest_codegen inspect-mocks
  inputs: headers/modules + compile database
  output: mock manifest

gentest_codegen emit-mocks
  input: mock manifest
  output: mock headers/modules/registries
```

Test registration may consume mock manifests, but mock extraction must not be
inseparable from test registration. This keeps explicit mocks usable for
non-test targets and gives non-CMake build systems one stable mock protocol.

The generic `inspect` / `emit` registration protocol and the mock-specific
`inspect-mocks` / `emit-mocks` protocol should not overlap. Mocks have their own
manifest, and registration consumes that manifest only when a test target
depends on generated mock surfaces.

## Scope

In scope:

- artifact manifest design for generated codegen outputs
- same-module implementation-unit registration for primary named-module tests
- codegen-owned validation of module names, source kinds, and imports
- build-system composition of generated artifacts without source parsing
- separation of mock extraction and mock emission phases
- textual wrapper registration alignment with the same artifact-manifest protocol
- first-slice rejection of private module fragments and partitions with clear
  `gentest_codegen` diagnostics

Out of scope for the first implementation slice:

- full module partition support
- private module fragment support
- public API redesign for fixture/runtime internals
- making every non-CMake backend feature-complete in the first commit
- making declaration-only standalone textual registration the default
- preserving CMake configure-time source parsers as a compatibility mechanism

## Tradeoffs

This approach intentionally gives up CMake-only convenience shortcuts:

- build systems need to predeclare output paths and then validate them through
  an inspect/manifest step
- multi-config generators need explicit per-config output policy or a manifest
  format that can describe config-specific outputs
- non-CMake backends must learn to consume the artifact manifest
- generated module implementation units must be compiled in the same target
  context as the owning module interface
- private module fragments and partitions need explicit later design instead of
  accidental support through source inclusion

It does not give up the important module behavior:

- authored module interface sources remain authored module interface sources
- test functions and fixtures do not need to be exported
- generated registration compiles as normal C++ code
- build systems do not need to understand C++ semantics
- compile databases can keep the original user translation units visible

It also does not give up the important textual behavior:

- textual `.cpp` tests can keep source-local fixtures and helper types
- anonymous-namespace and `static` tests remain valid in wrapper mode
- fixture, bench, jitter, and mock bodies do not need to move declarations into
  headers just to satisfy generated registration code
- include-wrapper behavior is explicit in the manifest instead of hidden in
  CMake, Bazel, Xmake, or Meson glue

Private module fragments are not just deferred convenience. A primary module
interface unit with `module :private;` must be the only unit of that module, so
additive same-module registration is ill-formed for that source shape. The first
slice must reject it in `gentest_codegen` rather than trying to generate a
separate `module <name>;` implementation unit.

Partitions also need explicit classification. The manifest must distinguish
primary interfaces from partitions, and the first slice must reject partition
inputs until partition-aware generation has a separate design.

Global module fragments are allowed only when generated registration does not
need names that exist solely in the authored unit's global module fragment. The
first slice should constrain tests and fixtures to the named-module purview, or
have codegen emit any generated-unit preamble it needs explicitly.

## Acceptance Criteria

- `gentest_codegen` can produce an artifact manifest before generated outputs
  are compiled, while build systems can still predeclare all generated outputs
  before finalizing their build graph.
- CMake consumes the manifest without parsing source text for module names,
  imports, preprocessor state, or mock semantics.
- Module test registration generates same-module implementation units and adds
  them to the test target without replacing the authored module interface units.
- A module test source with non-exported tests and fixtures registers and runs.
- The generated module registration unit has `module <name>;`, not
  `import <name>;` and not `#include "<source>.cppm"`.
- Private module fragments and module partitions are rejected by
  `gentest_codegen` in the first slice.
- Textual registration keeps manifest-declared wrapper/include mode as the
  default compatibility path, with explicit `includes_owner_source` and
  `replaces_owner_source` semantics.
- Declaration-only standalone textual registration is treated as a separate
  opt-in mode with stricter source-style constraints, not as a default
  acceptance criterion.
- Mock extraction can run independently from test registration and emits a
  reusable manifest.
- Existing `gentest_attach_codegen(...)`, package-consumer, public-module, and
  explicit-mock regression slices stay green or have explicit migration tests.

## Validation Target

Minimum first-slice validation:

- CMake module fixture where the authored module interface contains non-exported
  test functions and fixture types.
- CMake regression proving the generated registration artifact is a same-module
  implementation unit and does not include the original source.
- Compile database regression proving the authored module interface remains in
  the target compile database.
- CMake depfile/incremental regression proving an included header or relevant
  compile command change reruns inspect/emit.
- Package-consumer regression with `GENTEST_ENABLE_PUBLIC_MODULES=ON` or `AUTO`.
- Existing module/mock regressions covering explicit mock targets remain green.
- Bazel analysis regression proving outputs are declared before action
  execution and the emitted manifest matches those declared outputs.
- Xmake module proof with predeclared generated same-module registration source
  and manifest validation.
- Meson textual proof for declared wrapper outputs plus depfile wiring.
- Textual wrapper regression proving anonymous namespaces, `static` tests, and
  `.cpp`-local fixture definitions remain supported by the default path.

## Relationship to PR `75`

PR `75` proved that same-module additive registration is promising, but it also
added a second public API and a large CMake-side source/compile-definition
classifier. This story supersedes that direction.

The salvageable idea is the generated same-module registration implementation
unit. The non-salvageable part is asking CMake to decide C++ semantics.

## Relationship to Story `033`

Story `033` may split `GentestCodegen.cmake` into focused orchestration modules,
but those modules are transitional build-system adapters. `TuMode.cmake`,
`ExplicitMocks.cmake`, or any equivalent CMake helper must not become the owner
of artifact planning, module semantics, or mock semantics. This story owns the
future boundary: CMake composes artifacts; `gentest_codegen` decides what those
artifacts mean.

## Status

Partial.

- Mock extraction now has a tool-owned JSON manifest for the existing
  `MockClassInfo` model via `--mock-manifest-output`.
- Header-like/textual mock emission can consume that manifest via
  `--mock-manifest-input` without reparsing the original mock definition source.
- The public CLI now has explicit `inspect-mocks` and `emit-mocks` phase entry
  points, with `--mock-manifest-output` / `--mock-manifest-input` preserved as
  backward-compatible aliases.
- `inspect-mocks` is a manifest-only phase; legacy `--discover-mocks` one-shot
  codegen remains available through the existing flags.
- Named-module mock manifest-input emission now uses manifest-declared
  `mock_output_domain_modules` order and rejects named-module mocks omitted
  from that domain list, so output path mapping does not depend on mock sort
  order or reparsing source text.
- The split mock protocol still does not replace the integrated
  `--discover-mocks` path for module-wrapper source transformation and
  module-owned mock attachment injection.
