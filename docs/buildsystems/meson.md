# Meson

gentest now has an official downstream Meson wrap/subproject story for the
textual path. Named-module support is still intentionally unsupported in Meson.

For host Clang, sysroot, and cross-build guidance, see
[host_toolchain_sysroots.md](host_toolchain_sysroots.md).

## Supported contract

Meson support is textual-only. The downstream contract is:

- consume gentest as a subproject / wrap
- pass explicit host-tool paths:
  - `gentest_codegen_path`
  - `gentest_codegen_host_clang`
  - optional `gentest_codegen_clang_scan_deps`
- describe textual mocks and suites with Meson dictionaries
- load the helper with `subdir('subprojects/gentest/meson/textual')`

The gentest subproject now exports:

- `gentest_dep`
- `gentest_main_dep`
- `gentest_runtime_dep`
- `gentest_codegen`
- `gentest_codegen_tool_args`
- `gentest_public_include`
- `gentest_public_include_path`
- `gentest_third_party_include`
- `gentest_third_party_include_path`

Meson does not support user-defined functions, so the public helper is a
declarative `subdir()` fragment rather than a `gentest_add_mocks()` function.
The helper delegates wrapper sources, mock wrapper sources, and generated mock
public headers to `gentest_codegen` through explicit output paths. The textual
custom targets emit depfiles for generated mock and registration outputs so
Meson/Ninja can rebuild when codegen inputs change.

## Downstream wrap example

```meson
project('gentest_downstream_wrap_consumer', 'cpp', default_options: ['cpp_std=c++20'])

gentest_sp = subproject(
  'gentest',
  default_options: [
    'build_self_tests=false',
    'codegen_path=/abs/path/to/gentest_codegen',
    'codegen_host_clang=/opt/llvm/bin/clang++',
  ],
)

gentest_textual_mocks = [
  {
    'name': 'gentest_downstream_textual_mocks',
    'id': 'downstream_textual_mocks',
    'defs': 'tests/header_mock_defs.hpp',
    'public_header': 'gentest_downstream_mocks.hpp',
    'source_includes': ['tests'],
  },
]

gentest_textual_suites = [
  {
    'name': 'gentest_downstream_textual',
    'id': 'downstream_textual',
    'source': 'tests/cases.cpp',
    'main': files('tests/main.cpp'),
    'source_includes': ['tests'],
    'mocks': ['gentest_downstream_textual_mocks'],
    'test_name': 'meson_downstream_textual',
  },
]

subdir('subprojects/gentest/meson/textual')
```

`source` and `defs` are source-root-relative strings used by `gentest_codegen`.
Extra executable sources such as `main` should be passed as caller-owned Meson
file objects, for example `files('tests/main.cpp')`, because the helper itself
lives under the gentest subproject.

The helper accepts these textual mock fields:

- `name`
- `id` (optional, used for generated file names)
- `kind` (optional, defaults to `textual`; any other value fails configure)
- `defs`
- `public_header`
- `source_includes` (list)
- `clang_args` (list)
- `cpp_args` (list)
- `dependencies` (list)
- `include_directories` (list)
- `install`
- `build_by_default`

It accepts these textual suite fields:

- `name`
- `id` (optional, used for generated file names)
- `kind` (optional, defaults to `textual`; any other value fails configure)
- `source`
- `main`
- `source_includes` (list)
- `mocks` (list)
- `test_name`
- `clang_args` (list)
- `cpp_args` (list)
- `dependencies` (list)
- `include_directories` (list)
- `link_with` (list)
- `install`
- `build_by_default`

After `subdir()`, the helper exposes lookup dictionaries named
`gentest_textual_mock_targets`, `gentest_textual_mock_codegen_targets`,
`gentest_textual_mock_public_headers`, `gentest_textual_suite_targets`, and
`gentest_textual_suite_codegen_targets`.

The checked-in downstream fixture in
[`tests/downstream/meson_wrap_consumer`](../../tests/downstream/meson_wrap_consumer)
shows the full textual mock + suite flow.

Minimal downstream layout:

```text
your_project/
  meson.build
  meson_options.txt
  subprojects/
    gentest/
      meson.build
      meson_options.txt
      include/
      src/
      meson/
      third_party/include/
  tests/
    main.cpp
    cases.cpp
    header_mock_defs.hpp
    service.hpp
```

The checked-in proof stages `subprojects/gentest` directly from this repo. A
real wrap package can provide the same subproject shape.

## Configure and build

These commands are for a downstream project that already has the layout shown
above, including `subprojects/gentest`. The checked-in fixture is not built
directly from its source directory; the CTest proof stages it into a temporary
workspace first.

```bash
meson setup build/meson-downstream . \
  -Dgentest_codegen_path=/abs/path/to/gentest_codegen \
  -Dgentest_codegen_host_clang=/opt/llvm/bin/clang++ \
  -Dgentest_codegen_clang_scan_deps=/opt/llvm/bin/clang-scan-deps

meson compile -C build/meson-downstream
meson test -C build/meson-downstream --print-errorlogs
build/meson-downstream/subprojects/gentest/meson/textual/gentest_downstream_textual --list
```

The final Meson compiler can remain non-Clang. The explicit host Clang contract
only applies to `gentest_codegen`.

To run the checked-in proof instead of a prepared downstream project:

```bash
ctest --preset=debug-system --output-on-failure -R '^gentest_meson_wrap_consumer$'
```

## Checked-in proofs

The downstream proof in
[`tests/cmake/scripts/CheckMesonWrapConsumer.cmake`](../../tests/cmake/scripts/CheckMesonWrapConsumer.cmake):

- creates a real downstream workspace with `subprojects/gentest`
- configures gentest with `build_self_tests=false`
- builds the textual mock target + textual consumer
- verifies generated mock/codegen artifacts and their declared depfile outputs
- runs the consumer test/mock/bench/jitter surface

## Validated platforms

CI validates the Meson downstream wrap path on Ubuntu 24.04 and Fedora 43. The
checked-in wrap proof intentionally skips Windows today.

## Limitations

- Meson support is textual-only.
- The helper is a declarative `subdir()` API because Meson does not provide
  user-defined functions.
- Set `kind` to `textual` or omit it. `kind = 'modules'` intentionally fails
  at configure time.
- Windows is currently skipped for the downstream wrap proof.
- Named-module support remains intentionally unsupported.
- Support headers/fragments are snapshotted at configure time, so adding new
  included support files requires `meson setup --reconfigure`.
