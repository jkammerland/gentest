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
- use the exported Meson variables from the gentest subproject

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
- `gentest_wrapper_template`
- `gentest_textual_mock_template`
- `gentest_anchor_template`
- `gentest_textual_public_header_template`

This is a lower-level wrap contract than CMake or Xmake. There is still no
high-level `gentest_add_mocks()` Meson function, but downstream textual
consumption no longer depends on copying repo-private files by hand.

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

gentest_codegen = gentest_sp.get_variable('gentest_codegen')
gentest_codegen_tool_args = gentest_sp.get_variable('gentest_codegen_tool_args')
gentest_main_dep = gentest_sp.get_variable('gentest_main_dep')
gentest_runtime_dep = gentest_sp.get_variable('gentest_runtime_dep')
gentest_wrapper_template = gentest_sp.get_variable('gentest_wrapper_template')
gentest_textual_mock_template = gentest_sp.get_variable('gentest_textual_mock_template')
gentest_anchor_template = gentest_sp.get_variable('gentest_anchor_template')
gentest_textual_public_header_template = gentest_sp.get_variable('gentest_textual_public_header_template')
```

The checked-in downstream fixture in
[`tests/downstream/meson_wrap_consumer`](../../tests/downstream/meson_wrap_consumer)
shows the full textual mock + suite flow.

## Configure and build

```bash
meson setup build/meson-downstream tests/downstream/meson_wrap_consumer \
  -Dgentest_codegen_path=/abs/path/to/gentest_codegen \
  -Dgentest_codegen_host_clang=/opt/llvm/bin/clang++ \
  -Dgentest_codegen_clang_scan_deps=/opt/llvm/bin/clang-scan-deps

meson compile -C build/meson-downstream
meson test -C build/meson-downstream --print-errorlogs
```

The final Meson compiler can remain non-Clang. The explicit host Clang contract
only applies to `gentest_codegen`.

## Checked-in proofs

The downstream proof in
[`cmake/CheckMesonWrapConsumer.cmake`](../../cmake/CheckMesonWrapConsumer.cmake):

- creates a real downstream workspace with `subprojects/gentest`
- configures gentest with `build_self_tests=false`
- builds the textual mock target + textual consumer
- verifies generated mock/codegen artifacts
- runs the consumer test/mock/bench/jitter surface

## Limitations

- Meson support is textual-only.
- There is still no high-level Meson macro API matching
  `gentest_add_mocks()` / `gentest_attach_codegen()`.
- Windows is currently skipped for the downstream wrap proof.
- Named-module support remains intentionally unsupported.
