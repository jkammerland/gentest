# Story: explicit host-Clang contract for `gentest_codegen`

## Goal

Make `gentest_codegen` officially usable when the consumer toolchain is not
Clang by separating:

- the target compiler/toolchain used to build the final binary
- the host Clang toolchain used by `gentest_codegen` for Clang-only work

This story turns the current best-effort fallback behavior into an explicit,
documented, cross-buildsystem contract.

## Problem statement

Today, parts of the product and CI still rely on `CC` / `CXX` doubling as both:

- the target compiler for the final test binary
- the host Clang used by `gentest_codegen`

That is not a stable contract for official support.

Real downstreams will often have:

- GCC or `g++` as the final Linux compiler
- `cl.exe` / MSVC as the final Windows compiler
- `qcc` / `q++` as the final QNX compiler
- a separate host Clang installation only for codegen and module scanning

The current fallback logic in
[`tools/src/main.cpp`](../../tools/src/main.cpp) is good enough to recover some
GCC-like flows, but that is not the same thing as having an explicit,
predictable user contract.

## Product direction

The public contract should become:

1. `CC` / `CXX` select the target compiler/toolchain.
2. `gentest_codegen` gets an explicit host-Clang setting for Clang-only steps.
3. `clang-scan-deps` remains separately configurable.
4. Buildsystem helpers pass the host-Clang knob explicitly when they know it.
5. Fallback probing remains as compatibility behavior, not the primary API.

The unifying knob should be:

- CLI: `--host-clang=<path>`
- env fallback: `GENTEST_CODEGEN_HOST_CLANG`

Buildsystem-specific frontends should map to the same tool behavior:

- CMake cache: `GENTEST_CODEGEN_HOST_CLANG`
- Bazel helper surface: `codegen_host_clang` and/or
  `GENTEST_CODEGEN_HOST_CLANG`
- Xmake/xrepo helper surface: `codegen = { clang = ..., scan_deps = ... }`
- Meson option/helper surface: `codegen_host_clang`

Existing scan-deps knobs stay valid:

- CLI: `--clang-scan-deps=<path>`
- env: `GENTEST_CODEGEN_CLANG_SCAN_DEPS`
- CMake: `GENTEST_CODEGEN_CLANG_SCAN_DEPS`

## Why this is the right contract

`gentest_codegen` needs a real Clang executable path for:

- built-in header/resource-dir lookup
- named-module precompile
- named-module dependency scanning

Those are host-tool concerns.

They should not depend on whether the consumer uses:

- GCC
- MSVC
- QNX `qcc` / `q++`
- Clang

The consumer compile database still matters for:

- include paths
- defines
- target/sysroot flags
- language mode

But the driver executable in that compile database is not trustworthy enough to
be the source of truth for Clang-only work unless it is already explicitly
clang-like.

## User stories

### 1. CMake + GCC consumer

As a Linux user building tests with GCC, I want `gentest_attach_codegen()` to
keep using my GCC target compiler while I point codegen at a separate host
Clang, so modules and codegen keep working without lying through `CXX=clang++`.

### 2. CMake + MSVC consumer

As a Windows user building with `cl.exe`, I want to point `gentest_codegen` at
`clang-cl.exe` explicitly, so codegen and module flows do not depend on ambient
PATH resolution or accidental `clang++` availability.

### 3. Bazel + non-Clang consumer

As a Bazel consumer, I want the gentest rules to take a dedicated host-Clang
setting for codegen while leaving Bazel's target C++ toolchain alone, so the
module lane does not force `CC` / `CXX` to be Clang.

### 4. Xmake/xrepo + non-Clang consumer

As an Xmake user, I want the helper layer to accept an explicit codegen host
Clang path and scan-deps path distinct from the configured target toolchain, so
the Xmake package can support GCC/MSVC-style downstreams honestly.

### 5. Meson + textual consumer

As a Meson user, I want the textual codegen path to accept an explicit
host-Clang setting even when the final build uses GCC or another non-Clang
compiler, so the current textual-only support remains predictable.

### 6. Linux to aarch64 cross + QEMU

As a cross-compiling user, I want the target sysroot/triple to stay on the
final build toolchain while `gentest_codegen` uses a host Clang binary, so
cross builds can keep working under QEMU without requiring the target compiler
to be Clang.

### 7. Future QNX consumer

As a QNX user, I want a path toward explicit `qcc` / `q++` support where the
target compiler remains QNX-native and codegen uses host Clang explicitly, so
we can add official support without overloading `CC` / `CXX`.

## Selected design

### Core CLI

Add:

- `--host-clang=<path>`
- env fallback `GENTEST_CODEGEN_HOST_CLANG`

Resolution order inside `gentest_codegen`:

1. explicit `--host-clang`
2. buildsystem-provided explicit host-Clang setting
3. `GENTEST_CODEGEN_HOST_CLANG`
4. explicit clang-like compiler path from the compile database
5. legacy fallback probing

Rules:

- non-Clang drivers from the compile database must never be used directly for
  resource-dir lookup, named-module precompile, or `clang-scan-deps`
- if an explicit host-Clang is present, it should win over bare `clang++`
  names from the compile database
- fallback probing remains for compatibility, but module/scan-deps flows should
  fail clearly when no usable host Clang exists

### CMake

Add:

- cache variable `GENTEST_CODEGEN_HOST_CLANG`

`GentestCodegen.cmake` should pass:

- `--host-clang "${GENTEST_CODEGEN_HOST_CLANG}"` when configured

The existing `CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS` fallback can remain for
scan-deps only.

### Bazel

Repo-local helper direction:

- expose `codegen_host_clang` on the helper macros/rules
- also support `GENTEST_CODEGEN_HOST_CLANG` as an environment fallback for the
  checked-in CI smoke lane
- stop treating `CC` / `CXX` as the codegen-host contract in docs and CI

The target Bazel toolchain remains independent.

### Xmake/xrepo

Add explicit codegen tool settings under `gentest_configure()`:

```lua
codegen = {
    exe = ".../gentest_codegen",
    clang = ".../clang++",
    scan_deps = ".../clang-scan-deps",
}
```

Fallbacks may still consult environment variables, but the documented contract
should no longer be "set `CC` / `CXX` to Clang".

### Meson

Keep the current textual-only scope, but add an explicit host-Clang input to
the repo-local helper flow:

- Meson option `-Dcodegen_host_clang=...`
- optional `-Dcodegen_clang_scan_deps=...`

Meson named-module support remains intentionally unsupported.

## Solution sketch

### CMake with GCC consumer, Clang host tool

```cmake
set(CMAKE_CXX_COMPILER /usr/bin/g++)
set(GENTEST_CODEGEN_EXECUTABLE /opt/gentest/bin/gentest_codegen)
set(GENTEST_CODEGEN_HOST_CLANG /opt/llvm/bin/clang++)
set(GENTEST_CODEGEN_CLANG_SCAN_DEPS /opt/llvm/bin/clang-scan-deps)

find_package(gentest CONFIG REQUIRED)

add_executable(my_tests main.cpp cases.cppm)
target_link_libraries(my_tests PRIVATE gentest::gentest gentest::gentest_main)

gentest_attach_codegen(my_tests
  CLANG_ARGS
    "--target=aarch64-linux-gnu"
    "--sysroot=/opt/sdk/sysroots/aarch64")
```

### CMake with `cl.exe` consumer and `clang-cl` host tool

```cmake
set(CMAKE_CXX_COMPILER cl)
set(GENTEST_CODEGEN_EXECUTABLE "C:/gentest/bin/gentest_codegen.exe")
set(GENTEST_CODEGEN_HOST_CLANG "C:/LLVM/bin/clang-cl.exe")
set(GENTEST_CODEGEN_CLANG_SCAN_DEPS "C:/LLVM/bin/clang-scan-deps.exe")
```

### CMake with QNX `q++` consumer

```cmake
set(CMAKE_CXX_COMPILER /opt/qnx/host/linux/x86_64/usr/bin/q++)
set(GENTEST_CODEGEN_HOST_CLANG /opt/llvm/bin/clang++)
set(GENTEST_CODEGEN_CLANG_SCAN_DEPS /opt/llvm/bin/clang-scan-deps)

gentest_attach_codegen(my_tests
  CLANG_ARGS
    "--target=aarch64-unknown-nto-qnx7.1.0"
    "--sysroot=/opt/qnx/target/qnx7")
```

### Bazel

```python
gentest_attach_codegen_modules(
    name = "my_tests",
    src = "tests/cases.cppm",
    main = "tests/main.cpp",
    mock_targets = [":my_mocks"],
    codegen_host_clang = "/opt/llvm/bin/clang++",
    clang_args = [
        "--target=aarch64-linux-gnu",
        "--sysroot=/opt/sdk/sysroots/aarch64",
    ],
)
```

### Xmake/xrepo

```lua
gentest_configure({
    codegen = {
        exe = "/opt/gentest/bin/gentest_codegen",
        clang = "/opt/llvm/bin/clang++",
        scan_deps = "/opt/llvm/bin/clang-scan-deps",
    },
    gentest_common_cxxflags = {
        "--target=aarch64-linux-gnu",
        "--sysroot=/opt/sdk/sysroots/aarch64",
    },
})
```

### Meson

```bash
meson setup build/meson \
  -Dcodegen_path=/opt/gentest/bin/gentest_codegen \
  -Dcodegen_host_clang=/opt/llvm/bin/clang++ \
  -Dcodegen_clang_scan_deps=/opt/llvm/bin/clang-scan-deps
```

## CI and test inventory work

Before calling this story done, scan and unify all places that still implicitly
use `CC` / `CXX` as the codegen host contract.

That includes at least:

- [`cmake/GentestCodegen.cmake`](../../cmake/GentestCodegen.cmake)
- [`build_defs/gentest.bzl`](../../build_defs/gentest.bzl)
- [`xmake/gentest.lua`](../../xmake/gentest.lua)
- [`meson.build`](../../meson.build)
- Bazel CMake smoke checks:
  - [`tests/cmake/scripts/CheckBazelTextualConsumer.cmake`](../../tests/cmake/scripts/CheckBazelTextualConsumer.cmake)
  - [`tests/cmake/scripts/CheckBazelModuleConsumer.cmake`](../../tests/cmake/scripts/CheckBazelModuleConsumer.cmake)
- Xmake CMake smoke checks:
  - [`tests/cmake/scripts/CheckXmakeTextualConsumer.cmake`](../../tests/cmake/scripts/CheckXmakeTextualConsumer.cmake)
  - [`tests/cmake/scripts/CheckXmakeTextualConsumerRegistration.cmake`](../../tests/cmake/scripts/CheckXmakeTextualConsumerRegistration.cmake)
  - [`tests/cmake/scripts/CheckXmakeModuleConsumer.cmake`](../../tests/cmake/scripts/CheckXmakeModuleConsumer.cmake)
  - Use explicit test-only target-toolchain knobs for GCC-target coverage:
    `GENTEST_XMAKE_TEST_TARGET_CC` and `GENTEST_XMAKE_TEST_TARGET_CXX`
- repo-local buildsystem workflows:
  - [`.github/workflows/buildsystems_linux.yml`](../../.github/workflows/buildsystems_linux.yml)
  - [`.github/workflows/cmake.yml`](../../.github/workflows/cmake.yml)
  - [`.github/workflows/cross_qemu.yml`](../../.github/workflows/cross_qemu.yml)
- downstream docs:
  - [`docs/buildsystems/host_toolchain_sysroots.md`](../buildsystems/host_toolchain_sysroots.md)
  - [`docs/buildsystems/bazel.md`](../buildsystems/bazel.md)
  - [`docs/buildsystems/xmake.md`](../buildsystems/xmake.md)
  - [`docs/buildsystems/meson.md`](../buildsystems/meson.md)

## Verification matrix

Required proof for this story:

1. Linux:
   - codegen retargeting regression
   - forced non-Clang fallback regression
   - GCC package/module regression
   - Bazel textual and module consumer smoke checks
   - Xmake textual and module consumer smoke checks
2. macOS:
   - Bazel textual and module consumer smoke checks under Homebrew `llvm@20`
   - Bazel textual and module consumer smoke checks under Homebrew `llvm@21`
   - Bazel textual and module consumer smoke checks under Homebrew `llvm`
   - Xmake textual and module smoke checks
3. Windows:
   - targeted CMake/package/codegen regressions
   - native Bazel textual and module consumer smoke checks
4. Linux to aarch64 QEMU:
   - `cmake --preset=aarch64-qemu`
   - `cmake --build --preset=aarch64-qemu`
   - `ctest --preset=aarch64-qemu --output-on-failure`

## Acceptance criteria

- `gentest_codegen` accepts an explicit host-Clang path and uses it for
  Clang-only operations.
- CMake exposes the same contract with `GENTEST_CODEGEN_HOST_CLANG`.
- Bazel/Xmake/Meson repo-local helpers stop documenting `CC` / `CXX` as the
  primary codegen-host contract.
- Existing fallback regressions stay green.
- New coverage proves explicit host-Clang behavior for at least GCC-style and
  MSVC-style consumer flows.
- The macOS Homebrew LLVM CI smoke lanes remain green.
- Linux aarch64 QEMU cross flow remains green.
