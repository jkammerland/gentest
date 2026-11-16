# Codegen artifact protocols

This page is a reference for connecting `gentest_codegen` to a build system:
the command lines codegen accepts, the files it writes, and the JSON manifests
that describe them. gentest's CMake, Meson, Xmake, and Bazel support follow
these protocols; to use one of those, see
[Choosing a build system](buildsystems/downstream_consumers.md) instead.

## Rules for a build integration

- Decide every output path before codegen runs, and pass each one on the
  command line. Codegen writes a valid source even when an output has nothing
  to register.
- Give codegen the compile commands of its inputs: a compilation database
  (`--compdb <dir>`, the directory that holds `compile_commands.json`) or
  compiler arguments after `--`. With a database, the arguments after `--` are
  added to each command.
- Don't parse C++ sources to classify modules, find mocks, or decide what to
  register. Codegen does that.
- Treat the manifests codegen writes as outputs to check, not as input for
  planning the build.
- Pass `--depfile <file>`. The depfile names codegen's outputs as targets and
  every file codegen read as a dependency.

## Manifests

| Manifest | `schema` value | Schema | Written by |
| --- | --- | --- | --- |
| Artifact manifest | `gentest.artifact_manifest.v1` | [gentest.artifact_manifest.v1.schema.json](schemas/gentest.artifact_manifest.v1.schema.json) | `--artifact-manifest <file>`, for header and module registration |
| Mock manifest | `gentest.mock_manifest.v1` | [gentest.mock_manifest.v1.schema.json](schemas/gentest.mock_manifest.v1.schema.json) | `gentest_codegen inspect-mocks --mock-manifest-output <file>` |

The `schema` value is part of the protocol. Codegen rejects a manifest that
carries any other value.

## Header registration

Header registration is gentest's default: annotated declarations live in
headers, and codegen writes registration sources that the build adds to the test
target next to its own sources. Each input is a *scan slot*, and each slot gets
one registration source. The option names say `textual` where they mean header
registration.

In this example, `tests/cases.cpp` includes `tests/cases.hpp`, which declares
the annotated cases. The header is also listed as a slot of its own:

```bash
gentest_codegen \
  --source-root . \
  --tu-out-dir gen \
  --textual-registration-output gen/tu_0000_cases.header_registration.gentest.cpp \
  --scan-slot-kind authored-tu \
  --compile-context-id my_tests:tests/cases.cpp \
  --textual-registration-output gen/tu_0001_cases.header_registration.gentest.cpp \
  --scan-slot-kind fallback-header \
  --compile-context-id my_tests:tests/cases.hpp \
  --artifact-manifest gen/my_tests.artifact_manifest.json \
  --depfile gen/my_tests.gentest.d \
  tests/cases.cpp tests/cases.hpp \
  -- -std=c++20 -I. -I/path/to/gentest/include
```

Compile `tests/cases.cpp` as usual and add both generated sources to the same
target, compiled with the same flags.

| Option | Meaning |
| --- | --- |
| `--tu-out-dir <dir>` | Directory for generated files. Required. |
| `--textual-registration-output <file>` | The registration source for one input. Repeat it once per input, in input order. |
| `--scan-slot-kind <kind>` | `authored-tu` for a source file of the target, `fallback-header` for a header listed in the target's sources. Repeat it once per input. |
| `--compile-context-id <id>` | The compile context of one input, recorded in the manifest. The bundled integrations pass `<target>:<source>`. Repeat it once per input. |
| `--artifact-manifest <file>` | Where to write the artifact manifest. |
| `--depfile <file>` | Where to write the depfile. |
| `--source-root <dir>` | Root for the relative source paths gentest records for each case. |
| `--compdb <dir>` | Directory that holds `compile_commands.json`. Without it, codegen builds each file's command from the host Clang and the arguments after `--`; see [Host toolchains and cross-compiling](buildsystems/host_toolchain_sysroots.md). |

### What codegen writes

- A non-empty registration source includes only the annotated headers and
  gentest's registration support. It never includes a `.cpp`; the linker joins
  the registrations with the definitions.
- A slot that owns no case gets a source that contains only a comment.
- Codegen scans `authored-tu` slots first. It scans a `fallback-header` slot
  only when no authored slot reached that header through its includes, so in
  the example `tu_0001_cases.header_registration.gentest.cpp` stays empty. A
  header-only target has only `fallback-header` slots.
- Codegen spells each header include as follows. When the source part of
  `--compile-context-id` is a relative path and the header's path contains
  that source's directory, the include starts at that directory. Otherwise,
  when `--source-root` is relative, the include is relative to it. Otherwise it
  is relative to the generated source. CMake passes absolute ids and an
  absolute source root, so its includes are relative to the generated source.
  In the example the generated source includes `"tests/cases.hpp"`, so the
  arguments contain `-I.`.

The artifact manifest has one `sources` entry and one `artifacts` entry per
slot:

- Each source has `kind: "cxx-header-declaration-registration"`, its
  `scan_source`, its `scan_slot_kind`, and a `compile_context_fingerprint`: a
  `sha256:` hash of the slot's normalized compile flags.
- Each artifact has `compile_as: "cxx-header-declaration-registration"`,
  `target_attachment: "append-generated-source"`,
  `includes_authored_source: false`, `replaces_authored_source: false`, and the
  same fingerprint as its source.

### Compile commands and fingerprints

Codegen takes each slot's compile context from the command for its registration
output, so a compilation database must hold exactly one command for each
registration output. Codegen computes the fingerprint from that command, then
scans the input with an equivalent command: the input's own entry when one has
the same fingerprint, or the registration output's command pointed at the input.
It also checks that the final scan command keeps every flag that affects
meaning, in order. If any check fails, codegen stops with an error that names
the slot.

## Module registration

Module registration covers cases declared in a named module. For each primary
module interface unit, codegen writes a header and an ordinary C++ source that
imports the module and registers its exported cases. CMake selects it with
`gentest_attach_codegen(<target> MODULE_REGISTRATION FILE_SET <name>)`; the
rules for module tests are in [Named modules](modules.md).

```bash
gentest_codegen \
  --compdb build \
  --clang-scan-deps clang-scan-deps \
  --tu-out-dir gen \
  --tu-header-output gen/tu_0000_cases.gentest.h \
  --module-registration-output gen/tu_0000_cases.registration.gentest.cpp \
  --compile-context-id my_tests:tests/cases.cppm \
  --artifact-manifest gen/my_tests.artifact_manifest.json \
  --depfile gen/my_tests.gentest.d \
  tests/cases.cppm \
  -- -std=c++20 -I/path/to/gentest/include
```

- Named-module inputs need `--clang-scan-deps <path>` (or the
  `GENTEST_CODEGEN_CLANG_SCAN_DEPS` environment variable) and an entry in the
  compilation database. Without them codegen stops with
  `named-module codegen requires clang-scan-deps` or
  `no compilation database entry available for '<file>'`.
- An install with public modules puts `gentest.cppm` and `gentest.mock.cppm`
  in `include/gentest/`, so codegen finds those modules through the include
  path.
- Pass one `--tu-header-output` and one `--module-registration-output` per
  input, in input order.

The generated source imports the module and includes the generated header. It
never includes the `.cppm` and never declares a module. Compile it as part of
the test target.

Its artifact entry has `compile_as: "cxx-module-importer-registration"`,
`imports: ["<module>"]`, `target_attachment: "private-generated-source"`, and
`requires_module_scan: true`, and lists the generated header and its directory
in `generated_headers` and `generated_include_dirs`. The source entry has
`kind: "module-primary-interface"`, the module name, and `partition: null`.

Cases and the fixture types they use must be exported from the module, or codegen
reports that they aren't reachable after importing it. Codegen also can't
generate a mock for a type that a module-registration input owns. To catch
this, CMake runs `inspect-mocks` on the same inputs and passes the result with
`--mock-registration-manifest <file>`; codegen then fails with
`MODULE_REGISTRATION cannot generate direct mocks owned by named module '<module>'`.
Generate the mock in a mock target instead, as [Named modules](modules.md)
describes.

## Mocks

A mock target generates the code behind `gentest::mock<T>` from a mock
definitions header (`DEFS`), or from a mock definitions module for named
modules. The bundled integrations generate mocks in one codegen run. Codegen can
also split the work into two runs around a mock manifest.

### One run

Pass `--discover-mocks`, the mock output paths, and the definitions. For a mock
definitions header, `--mock-public-header` names the header that tests include:

```bash
gentest_codegen \
  --source-root . \
  --discover-mocks \
  --tu-out-dir gen \
  --tu-header-output gen/tu_0000_service_mocks.gentest.h \
  --mock-registry gen/service_mock_registry.hpp \
  --mock-impl gen/service_mock_impl.hpp \
  --mock-domain-registry-output gen/service_mock_registry__domain_0000_header.hpp \
  --mock-domain-impl-output gen/service_mock_impl__domain_0000_header.hpp \
  --mock-public-header gen/service_mocks.hpp \
  --depfile gen/service_mocks.d \
  tests/service_mocks.hpp \
  -- -std=c++20 -I. -I/path/to/gentest/include
```

Codegen also writes one header per input. Name it with `--tu-header-output`;
otherwise it is `<tu-out-dir>/<input stem>.h`.

### Two runs around a mock manifest

`inspect-mocks` finds the mocks and writes a mock manifest. `emit-mocks` reads
the manifest and writes the mock headers, but not `--mock-public-header`. The
two runs are separate build steps:

```bash
gentest_codegen inspect-mocks \
  --mock-manifest-output gen/service.mock_manifest.json \
  tests/service_mocks.hpp \
  -- -std=c++20 -I. -I/path/to/gentest/include

gentest_codegen emit-mocks \
  --mock-manifest-input gen/service.mock_manifest.json \
  --mock-registry gen/service_mock_registry.hpp \
  --mock-impl gen/service_mock_impl.hpp \
  --mock-domain-registry-output gen/service_mock_registry__domain_0000_header.hpp \
  --mock-domain-impl-output gen/service_mock_impl__domain_0000_header.hpp \
  --depfile gen/service_mocks.d
```

`emit-mocks` rejects a manifest with another `schema` value, and a named-module
mock whose module isn't one of the manifest's module domains.

### Mock output domains

Mock headers are split into domains, and each domain needs one
`--mock-domain-registry-output` and one `--mock-domain-impl-output`:

- The first pair is always the header domain, for mocked types outside named
  modules.
- Then one pair follows for each named module among the inputs, in input order.
  A mock manifest lists these modules in `mock_output_domain_modules`.

Codegen fails when the counts don't match, for example
`expected 3 --mock-domain-registry-output/--mock-domain-impl-output value(s) for discovered mock output domains, got 2 and 2`.
A build that knows its module names can declare all outputs without reading the
manifest. In this example, `tests/service.cppm` declares `demo.service`, which
defines the mocked type, and `tests/service_mocks.cppm` declares
`demo.service_mocks`:

```bash
gentest_codegen inspect-mocks \
  --compdb build \
  --clang-scan-deps clang-scan-deps \
  --mock-manifest-output gen/service.mock_manifest.json \
  tests/service.cppm tests/service_mocks.cppm \
  -- -std=c++20 -I/path/to/gentest/include

gentest_codegen emit-mocks \
  --mock-manifest-input gen/service.mock_manifest.json \
  --mock-registry gen/service_mock_registry.hpp \
  --mock-impl gen/service_mock_impl.hpp \
  --mock-domain-registry-output gen/service_mock_registry__domain_0000_header.hpp \
  --mock-domain-registry-output gen/service_mock_registry__domain_0001_demo_service.hpp \
  --mock-domain-registry-output gen/service_mock_registry__domain_0002_demo_service_mocks.hpp \
  --mock-domain-impl-output gen/service_mock_impl__domain_0000_header.hpp \
  --mock-domain-impl-output gen/service_mock_impl__domain_0001_demo_service.hpp \
  --mock-domain-impl-output gen/service_mock_impl__domain_0002_demo_service_mocks.hpp
```

### Mock aggregate module

A mock target with a mock definitions module publishes one generated module
that tests import. Codegen writes it when you pass
`--mock-aggregate-module-name` and `--mock-aggregate-module-output`. In a single
run with module inputs, codegen also writes, per input, a `--tu-header-output`
header and a `--module-wrapper-output` copy of the module unit, with the mock
code added to the module that defines each mocked type. The build compiles
those copies in place of the inputs:

```bash
gentest_codegen \
  --compdb build \
  --clang-scan-deps clang-scan-deps \
  --discover-mocks \
  --tu-out-dir gen \
  --tu-header-output gen/tu_0000_service.gentest.h \
  --module-wrapper-output gen/tu_0000_service.module.gentest.cppm \
  --tu-header-output gen/tu_0001_service_mocks.gentest.h \
  --module-wrapper-output gen/tu_0001_service_mocks.module.gentest.cppm \
  --mock-registry gen/service_mock_registry.hpp \
  --mock-impl gen/service_mock_impl.hpp \
  --mock-domain-registry-output gen/service_mock_registry__domain_0000_header.hpp \
  --mock-domain-registry-output gen/service_mock_registry__domain_0001_demo_service.hpp \
  --mock-domain-registry-output gen/service_mock_registry__domain_0002_demo_service_mocks.hpp \
  --mock-domain-impl-output gen/service_mock_impl__domain_0000_header.hpp \
  --mock-domain-impl-output gen/service_mock_impl__domain_0001_demo_service.hpp \
  --mock-domain-impl-output gen/service_mock_impl__domain_0002_demo_service_mocks.hpp \
  --mock-aggregate-module-name demo.mocks \
  --mock-aggregate-module-output gen/demo/mocks.cppm \
  --depfile gen/service_mocks.d \
  tests/service.cppm tests/service_mocks.cppm \
  -- -std=c++20 -I/path/to/gentest/include
```

The aggregate module re-exports `gentest`, `gentest.mock`, and every module
domain. It is a build output listed in the depfile; don't write it from the
mock definitions at configure time.

### External module sources

`--external-module-source=<module>=<path>` tells codegen where a named module's
interface source is. A consumer of an installed mock target may know only
provisional names for the generated module copies; pass them anyway. Codegen
reads each candidate and uses it only when the source declares the module it
needs. Without a mapping, codegen looks for the module interface among the
compilation database's files and in the include directories.

## Validating an artifact manifest

`gentest_codegen validate-artifact-manifest` checks a manifest against the
outputs the build declared. CMake runs it after codegen. This checks the header
registration example above:

```bash
gentest_codegen validate-artifact-manifest \
  --manifest gen/my_tests.artifact_manifest.json \
  --stamp gen/my_tests.manifest.stamp \
  --expected-source tests/cases.cpp \
  --expected-source-kind cxx-header-declaration-registration \
  --expected-scan-slot-kind authored-tu \
  --expected-registration-output gen/tu_0000_cases.header_registration.gentest.cpp \
  --expected-compile-context-id my_tests:tests/cases.cpp \
  --expected-source tests/cases.hpp \
  --expected-source-kind cxx-header-declaration-registration \
  --expected-scan-slot-kind fallback-header \
  --expected-registration-output gen/tu_0001_cases.header_registration.gentest.cpp \
  --expected-compile-context-id my_tests:tests/cases.hpp \
  --expected-depfile gen/my_tests.gentest.d \
  --expected-target-attachment append-generated-source \
  --expected-artifact-role registration \
  --expected-compile-as cxx-header-declaration-registration \
  --expected-requires-module-scan false \
  --expected-includes-authored-source false \
  --expected-replaces-authored-source false
```

- `--manifest`, `--expected-depfile`, `--expected-target-attachment`,
  `--expected-artifact-role`, `--expected-compile-as`, and
  `--expected-requires-module-scan` are required.
- The per-source options pair up by position. `--expected-scan-slot-kind`
  applies to header registration. For module registration, pass
  `--expected-header` per source and `--expected-include-dir`.
- `--compdb <dir>` also checks that each expected source has a compile command.
- On success the command exits with 0 and updates `--stamp`. Otherwise it prints
  the first mismatch, such as
  `sources[0].scan_slot_kind mismatch: expected 'fallback-header', got 'authored-tu'`,
  and exits with 1.

## Limits

Codegen rejects these inputs. [Writing tests](writing_tests.md) and
[Named modules](modules.md) explain the rules for test authors.

- Header registration: an annotation in a `.cpp` file, a case without external
  linkage (`static` or in an anonymous namespace), a non-template case defined
  in a header without `inline`, and a scanned source that imports a named
  module.
- Module registration: cases and fixture types that the module doesn't export.
  The inputs are primary module interface units; a manifest's `partition` is
  always `null`.

## Removed options

`--output`, `--template`, `--no-include-sources`, `--textual-wrapper-output`,
`--artifact-owner-source`, and `--scan-deps-mode` are removed.
[DEPRECATIONS.md](../DEPRECATIONS.md) lists what replaces them.

## Worker threads

`--jobs=<N>` sets the number of threads that scan slots and write registration
sources. 0, the default, uses the number of hardware threads. Without `--jobs`,
codegen reads `GENTEST_CODEGEN_JOBS`. The outputs are the same for any number
of threads.

## Design history

The design notes for [artifact manifests and module registration](stories/034_codegen_owned_artifact_manifest_and_module_registration.md)
and for [header registration](stories/036_textual_declaration_only_registration.md)
record why the protocol looks the way it does.
