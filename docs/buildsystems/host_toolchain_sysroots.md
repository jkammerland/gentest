# Host Clang, Sysroots, and `gentest_codegen`

This page describes the toolchain contract for downstream builds that use
`gentest_codegen`, especially when the target sysroot/toolchain differs from the
host machine that runs code generation.

## Core rule

Treat these as separate things:

- the **host Clang toolchain** used to run `gentest_codegen`
- the **target sysroot / target triple / target flags** used to compile the
  final test binary

`gentest_codegen` does more than parse source text. For module flows it also
needs a real host Clang executable for steps such as:

- built-in header/resource-dir lookup
- named-module precompile
- `clang-scan-deps`

So the stable contract is:

1. configure or pass an exact host `clang` / `clang++` path
2. optionally pass an exact host `clang-scan-deps` path
3. pass target sysroot / target flags through the normal build-system compile
   surface or the compile database
4. when cross-compiling, provide a **host-built** `gentest_codegen`

For background on why the exact host Clang path matters, see
[codegen_compiler_selection.md](../codegen_compiler_selection.md).

## Support matrix

| Build system | Current downstream status | Module path | What users should configure |
| --- | --- | --- | --- |
| CMake | primary / packaged | supported | set `GENTEST_CODEGEN_HOST_CLANG`; in cross builds also set `GENTEST_CODEGEN_EXECUTABLE` or `GENTEST_CODEGEN_TARGET` |
| Bazel | official Bzlmod / source-package support | supported but toolchain-sensitive | set per-target `codegen_host_clang` or export/pass through `GENTEST_CODEGEN_HOST_CLANG`; keep target flags in Bazel/C++ toolchain config |
| Xmake | official xrepo / installed-helper support | supported but toolchain-sensitive | set `codegen = { exe, clang, scan_deps }` or the matching env fallbacks; keep Xmake `cc` / `cxx` for the final target toolchain |
| Meson | official wrap/subproject textual support | intentionally unsupported | pass `-Dcodegen_path=...` and `-Dcodegen_host_clang=...`; textual-only today |

## CMake

CMake is still the cleanest path, but the stable codegen-host contract is now
explicit: use `GENTEST_CODEGEN_HOST_CLANG` for the host parser/compiler and use
the normal CMake compiler/toolchain surface for the final target build.

Project `CMakeLists.txt`:

```cmake
find_package(gentest CONFIG REQUIRED)

add_executable(my_tests
  main.cpp
  cases.cppm)

target_link_libraries(my_tests PRIVATE
  gentest::gentest
  gentest::gentest_main)

gentest_attach_codegen(my_tests
  CLANG_ARGS
    "--sysroot=/opt/sdk/targets/aarch64-sysroot")

gentest_discover_tests(my_tests)
```

Cross-compiling toolchain file:

```cmake
# toolchains/aarch64-clang.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSROOT "/opt/sdk/targets/aarch64-sysroot")

set(CMAKE_C_COMPILER "/opt/sdk/host-llvm/bin/clang")
set(CMAKE_CXX_COMPILER "/opt/sdk/host-llvm/bin/clang++")

set(CMAKE_C_COMPILER_TARGET "aarch64-linux-gnu")
set(CMAKE_CXX_COMPILER_TARGET "aarch64-linux-gnu")
```

Configure:

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=toolchains/aarch64-clang.cmake \
  -DGENTEST_CODEGEN_EXECUTABLE=/opt/sdk/host-tools/bin/gentest_codegen \
  -DGENTEST_CODEGEN_HOST_CLANG=/opt/sdk/host-llvm/bin/clang++ \
  -DGENTEST_CODEGEN_CLANG_SCAN_DEPS=/opt/sdk/host-llvm/bin/clang-scan-deps
```

Notes:

- Even when the final target compiler is GCC or another non-Clang toolchain,
  keep `GENTEST_CODEGEN_HOST_CLANG` pointed at the host `clang++` that should
  drive `gentest_codegen`.
- In native packaged CMake use, `gentest_attach_codegen()` can resolve an
  installed `gentest_codegen` automatically from the same prefix.
- In cross builds, the host code generator is not inferred; set
  `GENTEST_CODEGEN_EXECUTABLE` or `GENTEST_CODEGEN_TARGET`.
- Extra parsing flags for codegen can be passed through
  `gentest_attach_codegen(... CLANG_ARGS ...)` or
  `GENTEST_CODEGEN_DEFAULT_CLANG_ARGS`.

## Bazel

The Bazel downstream surface is source-package/Bzlmod based and still
toolchain-sensitive. The stable approach is to pass the host Clang toolchain
into Bazel actions explicitly.

Example `BUILD.bazel`:

```python
gentest_add_mocks_modules(
    name = "my_module_mocks",
    defs = [
        "tests/service.cppm",
        "tests/mock_defs.cppm",
    ],
    module_name = "demo.service_mocks",
)

gentest_attach_codegen_modules(
    name = "my_tests",
    src = "tests/cases.cppm",
    main = "tests/main.cpp",
    mock_targets = [":my_module_mocks"],
    deps = [
        ":gentest",
        ":gentest_bench_util",
    ],
    clang_args = [
        "--sysroot=/opt/sdk/targets/aarch64-sysroot",
        "--target=aarch64-linux-gnu",
    ],
    codegen_host_clang = "/opt/sdk/host-llvm/bin/clang++",
)
```

Build:

```bash
HOST_LLVM=/opt/sdk/host-llvm
HOST_CLANG="$HOST_LLVM/bin/clang++"
HOST_CC="$HOST_LLVM/bin/clang"
RES_DIR="$($HOST_CLANG -print-resource-dir)"
export GENTEST_CODEGEN_HOST_CLANG="$HOST_CLANG"
export GENTEST_CODEGEN_RESOURCE_DIR="$RES_DIR"

bazelisk build //:my_tests \
  --experimental_cpp_modules \
  --action_env=CC="$HOST_CC" \
  --action_env=CXX="$HOST_CLANG" \
  --action_env=GENTEST_CODEGEN_HOST_CLANG \
  --action_env=GENTEST_CODEGEN_RESOURCE_DIR \
  --host_action_env=CC="$HOST_CC" \
  --host_action_env=CXX="$HOST_CLANG" \
  --host_action_env=GENTEST_CODEGEN_HOST_CLANG \
  --host_action_env=GENTEST_CODEGEN_RESOURCE_DIR \
  --repo_env=CC="$HOST_CC" \
  --repo_env=CXX="$HOST_CLANG" \
  --repo_env=GENTEST_CODEGEN_HOST_CLANG \
  --repo_env=GENTEST_CODEGEN_RESOURCE_DIR
```

Notes:

- Do not rely on plain `PATH` discovery for the codegen-host lane.
- If the target already sets `codegen_host_clang` in `BUILD.bazel`, the
  `GENTEST_CODEGEN_HOST_CLANG` env pass-through becomes a repo-wide default or
  CI override instead of the only source of truth.
- Prefer putting target sysroot and target-triple flags in the C++ toolchain.
  When that is not available, pass them through `clang_args`.
- The checked-in downstream proof uses the public `@gentest//bazel:defs.bzl`
  entrypoint and a real `MODULE.bazel` fixture.

## Xmake

The Xmake downstream surface is install/xrepo based, and module builds still
require a Clang target toolchain. The stable codegen-host contract, however, is
the explicit `codegen = { ... }` block or the matching env fallbacks, not the
target `CC` / `CXX` pair.

Example `xmake.lua`:

```lua
includes("xmake/gentest.lua")

gentest_configure({
    project_root = os.projectdir(),
    incdirs = {"include", "tests", "third_party/include"},
    gentest_common_defines = {"FMT_HEADER_ONLY"},
    gentest_common_cxxflags = {
        "--sysroot=/opt/sdk/targets/aarch64-sysroot",
        "--target=aarch64-linux-gnu",
    },
    codegen = {
        exe = "/opt/sdk/host-tools/bin/gentest_codegen",
        clang = "/opt/sdk/host-llvm/bin/clang++",
        scan_deps = "/opt/sdk/host-llvm/bin/clang-scan-deps",
    },
})

target("my_module_mocks")
    set_kind("static")
    set_toolchains("llvm")
    gentest_add_mocks({
        name = "my_module_mocks",
        kind = "modules",
        defs = {"tests/service.cppm", "tests/mock_defs.cppm"},
        defs_modules = {"demo.service", "demo.mock_defs"},
        module_name = "demo.service_mocks",
        output_dir = "build/gen/mocks",
        deps = {"gentest"},
    })

target("my_tests")
    set_kind("binary")
    set_toolchains("llvm")
    gentest_attach_codegen({
        name = "my_tests",
        kind = "modules",
        source = "tests/cases.cppm",
        main = "tests/main.cpp",
        output_dir = "build/gen/tests",
        deps = {"gentest_main", "gentest", "my_module_mocks"},
    })
```

Configure:

```bash
xmake f -c -y \
  --toolchain=llvm \
  --cc=/opt/sdk/host-llvm/bin/clang \
  --cxx=/opt/sdk/host-llvm/bin/clang++
xmake b my_tests
```

On macOS, if Homebrew LLVM or another non-default LLVM prefix is used for the
module lane, also pass `--sdk=<llvm-prefix>` so Xmake does not fall back to the
Apple compiler/toolchain path.

Notes:

- Prefer the Lua-side `codegen = { exe, clang, scan_deps }` block when the
  project owns its `xmake.lua`; use `GENTEST_CODEGEN`,
  `GENTEST_CODEGEN_HOST_CLANG`, and `GENTEST_CODEGEN_CLANG_SCAN_DEPS` only when
  shell-level overrides are more convenient.
- If `codegen.exe` points into a CMake build tree, the helper can also reuse
  the adjacent `compile_commands.json`.
- Module consumers should treat explicit Clang selection as required, not
  best-effort.
- The checked-in downstream proof stages an install prefix and consumes it
  through a fixture-local xrepo repository.

## Meson

The Meson downstream surface is wrap/subproject based and textual-only.
Named-module targets are intentionally unsupported today, so Meson is only a
fit for classic or textual flows right now.

Native file:

```ini
[binaries]
c = '/opt/sdk/host-llvm/bin/clang'
cpp = '/opt/sdk/host-llvm/bin/clang++'

[built-in options]
c_args = ['--sysroot=/opt/sdk/targets/aarch64-sysroot']
cpp_args = ['--sysroot=/opt/sdk/targets/aarch64-sysroot']
```

Setup:

```bash
meson setup build/meson \
  --native-file clang.ini \
  -Dcodegen_path=/opt/sdk/host-tools/bin/gentest_codegen \
  -Dcodegen_host_clang=/opt/sdk/host-llvm/bin/clang++ \
  -Dcodegen_clang_scan_deps=/opt/sdk/host-llvm/bin/clang-scan-deps
meson compile -C build/meson
meson test -C build/meson --print-errorlogs
```

Notes:

- `-Dcodegen_path=...` and `-Dcodegen_host_clang=...` are the explicit Meson
  host-tool hooks.
- Keep target `c` / `cpp` compiler selection in the Meson native file or
  cross file. Do not use it as a substitute for the codegen-host contract.
- The checked-in downstream proof consumes `gentest` as a subproject with
  `build_self_tests=false` and runs a textual mock + suite consumer end to end.

## Packaging guidance

If this project is promoted to official downstream support across more build
systems, the clean direction is:

- CMake:
  install the runtime, public modules, `GentestCodegen.cmake`, and
  `gentest_codegen` in the same developer prefix.
- Bazel:
  publish a Bzlmod package / rule set that acquires a host `gentest_codegen`
  hermetically and takes toolchain inputs through Bazel configuration rather
  than ambient shell state.
- Xmake / xrepo:
  package the runtime, helper Lua layer, and host code generator together, or
  split the runtime and host-tool package explicitly.
- Meson:
  package the runtime first; add codegen helper packaging only after the Meson
  helper API is stabilized.

## About `target_install_package`

`target_install_package` is a good fit for packaging the `gentest` developer
surface itself.

That includes FHS-style installs such as:

- runtime and interface targets
- public modules and CMake helper files
- `gentest_codegen`
- supporting scripts, package metadata, and installers

That is different from trying to use package-install rules to solve the user's
compiler toolchain selection.

The boundary should be:

- `target_install_package` installs the `gentest` developer package, including
  host tools like `gentest_codegen`
- the consuming build system points at that installed host tool
- Clang / `clang-scan-deps` remain part of the user's configured host toolchain

Why that split still matters:

- `gentest_codegen` is a **host executable**
- Clang / `clang-scan-deps` are **host toolchain dependencies**
- cross-builds often need different host and target environments

So the clean packaging model is:

- package `gentest_codegen`, helper scripts, exports, and install metadata as
  part of the `gentest` install surface
- do not try to hide host compiler/toolchain setup inside target runtime
  package installation
