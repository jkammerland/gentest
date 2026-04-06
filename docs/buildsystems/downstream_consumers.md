# Downstream Consumers

This page is the shortest path to consuming `gentest` outside this repository.
Use it to choose the right integration, then jump into the backend-specific
guide for the exact files and commands.

For host Clang, sysroot, and cross-build guidance, see
[host_toolchain_sysroots.md](host_toolchain_sysroots.md).

## Pick a backend

| Build system | Downstream status | What you consume | Module support |
| --- | --- | --- | --- |
| CMake | primary / packaged | installed package + discoverable exact-match `fmt` package + `gentest_attach_codegen()` | supported |
| Bazel | official Bzlmod source-package | `@gentest//bazel:defs.bzl` | supported |
| Xmake / xrepo | official installed-helper / xrepo story | staged install prefix + `share/gentest/xmake/gentest.lua` | supported |
| Meson | official wrap/subproject textual story | subproject / wrap + exported Meson variables | textual only |

## Common contract

For installed-package CMake consumers, the runtime package also requires the
exact matching `fmt` CMake package to be discoverable. The simplest setup is to
install `gentest` and `fmt` into the same prefix and set `CMAKE_PREFIX_PATH`
once; otherwise, pass both the `gentest` package path and the matching
`fmt_DIR`.

All non-CMake downstream consumers need the same host-side codegen tools:

- `gentest_codegen`
- a host `clang++`
- optionally `clang-scan-deps`

Keep these separate from the final target compiler:

- `gentest_codegen` runs on the host machine
- the final test binary can use a different target compiler/toolchain

The stable rule is:

1. configure or pass the host `gentest_codegen`
2. configure or pass the host `clang++`
3. optionally configure `clang-scan-deps`
4. keep target sysroot / target flags in the normal build-system toolchain flow

## Quickstarts

### Bazel / Bzlmod

Use Bazel when you want a source-package integration and are comfortable
threading the host Clang toolchain into Bazel actions.

Minimal files:

- `MODULE.bazel`
- `BUILD.bazel`
- `tests/main.cpp`
- `tests/cases.cpp` and optionally `tests/cases.cppm`
- mock defs such as `tests/header_mock_defs.hpp` or `tests/module_mock_defs.cppm`

Load the public API from:

- `@gentest//bazel:defs.bzl`

Start here:

- [bazel.md](bazel.md)
- checked-in proof:
  [tests/downstream/bazel_bzlmod_consumer](../../tests/downstream/bazel_bzlmod_consumer)

### Xmake / xrepo

Use Xmake when you want an installed-prefix workflow with a reusable helper Lua
layer.

Minimal assets:

- installed prefix containing:
  - `bin/gentest_codegen`
  - `include/gentest/...`
  - `share/gentest/xmake/gentest.lua`
- project-local helper copy, usually:
  - `.gentest_support/gentest.lua`
- `xmake.lua`
- your `tests/...` sources

The checked-in downstream proof stages a real install prefix, copies
`share/gentest/xmake/` into `.gentest_support`, then consumes the staged prefix
through a fixture-local xrepo repository.

Start here:

- [xmake.md](xmake.md)
- checked-in proof:
  [tests/downstream/xmake_xrepo_consumer](../../tests/downstream/xmake_xrepo_consumer)

### Meson

Use Meson when you need textual-only downstream support.

Minimal assets:

- `meson.build`
- `meson_options.txt`
- `subprojects/gentest` or an equivalent wrap/subproject source
- your textual test and mock defs

The Meson downstream surface is intentionally lower-level than CMake/Xmake:
you consume exported variables from the `gentest` subproject and wire the
textual codegen targets yourself.

Start here:

- [meson.md](meson.md)
- checked-in proof:
  [tests/downstream/meson_wrap_consumer](../../tests/downstream/meson_wrap_consumer)

## What to read next

- CMake packaged consumer:
  [README.md](../../README.md)
- Bazel source-package guide:
  [bazel.md](bazel.md)
- Xmake / xrepo guide:
  [xmake.md](xmake.md)
- Meson wrap guide:
  [meson.md](meson.md)
- Host Clang and cross-toolchain contract:
  [host_toolchain_sysroots.md](host_toolchain_sysroots.md)
