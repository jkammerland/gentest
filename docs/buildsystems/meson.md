# Meson

This Meson support is still repo-local. The checked-in [`meson.build`](../../meson.build)
wires concrete targets for this repository; there is no installed Meson module
or reusable `gentest_add_mocks()` / `gentest_attach_codegen()` API yet.

For host Clang, sysroot, and cross-build configuration guidance, see
[host_toolchain_sysroots.md](host_toolchain_sysroots.md).

The intended Meson-facing contract is still the same 2-step model as the main
non-CMake story:

1. add explicit mocks
2. attach test codegen separately

`kind` is explicit in both operations. Meson currently supports only the
textual half of that contract.

The repo-local Meson contract for codegen host tools is:

- `-Dcodegen_path=...`
- `-Dcodegen_host_clang=...`
- optional `-Dcodegen_clang_scan_deps=...`

These options select the host-side tools used by `gentest_codegen`. They are
separate from Meson's target `c` / `cpp` compiler selection.

When `-Dcodegen_path=...` points at a repo-local CMake build of
`gentest_codegen`, the checked-in Meson wiring reuses the adjacent
`_deps/fmt-src/include` headers only as a fallback when Meson does not resolve a
system `fmt` dependency through `pkg-config`. That keeps the repo-local path
self-contained on hosts where `fmt` is not already visible to the build.

## Current repo-local surface

Meson currently defines these targets:

- classic suites:
  - `gentest_unit_meson`
  - `gentest_integration_meson`
  - `gentest_fixtures_meson`
  - `gentest_skiponly_meson`
- textual explicit-mock consumer:
  - `gentest_consumer_textual_meson`

There is no checked-in Meson module consumer target. Meson is textual-only for
now.

## Module API status

The repo-local Meson surface keeps the modules shape explicit, but named-module
targets are an intentional fail-fast boundary today. Meson textual integration
is native; Meson modules are deliberately unsupported until the backend is
reliable enough to justify a real checked-in path.

## Build and run

Prerequisites:

- `cmake`
- `meson`
- `ninja`
- `python3`
- LLVM/Clang development packages needed to build `gentest_codegen`

Build the host generator first:

```bash
cmake --preset=host-codegen
cmake --build --preset=host-codegen --parallel
```

Classic suites plus the textual consumer:

```bash
CC=gcc CXX=g++ \
meson setup build/meson \
  -Dcodegen_path=build/host-codegen/tools/gentest_codegen \
  -Dcodegen_host_clang=/opt/llvm/bin/clang++ \
  -Dcodegen_clang_scan_deps=/opt/llvm/bin/clang-scan-deps
meson compile -C build/meson
meson test -C build/meson --print-errorlogs
```

Using GCC above is intentional: the final Meson toolchain can stay non-Clang
while `gentest_codegen` uses the explicit host Clang path.

## Generated outputs

Classic suites still generate the usual per-TU wrapper pair in `build/meson/`:

- `tu_0000_<suite>_cases.gentest.cpp`
- `tu_0000_<suite>_cases.gentest.h`

The textual consumer also writes:

- `consumer_textual_mocks_defs.cpp`
- `consumer_textual_mocks_anchor.cpp`
- `tu_0000_consumer_textual_mocks_defs.gentest.h`
- `consumer_textual_mocks_mock_registry.hpp`
- `consumer_textual_mocks_mock_impl.hpp`
- `gentest_consumer_mocks.hpp`

## Limitations

- Repo-local only. There is no packaged Meson integration yet.
- There is no reusable Meson helper API for external/downstream projects yet.
- Meson named-module support is intentionally unsupported for now.
- `-Dcodegen_clang_scan_deps=...` is accepted for contract parity, but the
  checked-in Meson path remains textual-only.
- The current repo-local Meson path still snapshots support headers/fragments at
  configure time. If you add new included support files, rerun
  `meson setup --reconfigure ...` before compiling again.
- The checked-in surface keeps the modules boundary explicit, but the actual
  implementation still rejects Meson named-module targets on purpose.
