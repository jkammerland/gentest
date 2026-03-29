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
| CMake | primary / packaged | supported | configure Clang normally; when cross-compiling also set `GENTEST_CODEGEN_EXECUTABLE` or `GENTEST_CODEGEN_TARGET` |
| Bazel | repo-local helper today | supported but toolchain-sensitive | pass explicit host Clang/tooling into Bazel actions; do not rely on ambient `PATH` |
| Xmake | repo-local helper today | supported but toolchain-sensitive | configure Xmake with explicit Clang and, ideally, explicit `GENTEST_CODEGEN` |
| Meson | repo-local helper today | intentionally unsupported | pass `-Dcodegen_path=...`; textual-only today |

## CMake

CMake is the cleanest path because `gentest_attach_codegen()` already reuses the
active compile database and the configured C/C++ compiler surface.

Native or sysrooted build:

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
  -DGENTEST_CODEGEN_CLANG_SCAN_DEPS=/opt/sdk/host-llvm/bin/clang-scan-deps
```

Notes:

- In native packaged CMake use, `gentest_attach_codegen()` can resolve an
  installed `gentest_codegen` automatically from the same prefix.
- In cross builds, the host code generator is not inferred; set
  `GENTEST_CODEGEN_EXECUTABLE` or `GENTEST_CODEGEN_TARGET`.
- Extra parsing flags for codegen can be passed through
  `gentest_attach_codegen(... CLANG_ARGS ...)` or
  `GENTEST_CODEGEN_DEFAULT_CLANG_ARGS`.

## Bazel

The current Bazel support is repo-local and toolchain-sensitive. The stable
approach is to pass the host Clang toolchain into Bazel actions explicitly.

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
)
```

Build:

```bash
HOST_LLVM=/opt/sdk/host-llvm
CC_BIN="$HOST_LLVM/bin/clang"
CXX_BIN="$HOST_LLVM/bin/clang++"
RES_DIR="$($CXX_BIN -print-resource-dir)"

bazelisk build //:my_tests \
  --experimental_cpp_modules \
  --action_env=CC="$CC_BIN" \
  --action_env=CXX="$CXX_BIN" \
  --action_env=LLVM_DIR="$HOST_LLVM/lib/cmake/llvm" \
  --action_env=Clang_DIR="$HOST_LLVM/lib/cmake/clang" \
  --action_env=GENTEST_CODEGEN_RESOURCE_DIR="$RES_DIR" \
  --host_action_env=CC="$CC_BIN" \
  --host_action_env=CXX="$CXX_BIN" \
  --host_action_env=LLVM_DIR="$HOST_LLVM/lib/cmake/llvm" \
  --host_action_env=Clang_DIR="$HOST_LLVM/lib/cmake/clang" \
  --host_action_env=GENTEST_CODEGEN_RESOURCE_DIR="$RES_DIR" \
  --repo_env=CC="$CC_BIN" \
  --repo_env=CXX="$CXX_BIN"
```

Notes:

- Do not rely on plain `PATH` discovery for the module lane.
- Prefer putting target sysroot and target-triple flags in the C++ toolchain.
  When that is not available, pass them through `clang_args`.
- If this is promoted to official Bzlmod support later, the right direction is a
  proper Starlark module/rule set with hermetic host-tool acquisition instead of
  asking users to thread raw env vars everywhere.

## Xmake

The current Xmake support is repo-local and module builds require Clang. The
helper resolves the configured Xmake `cxx` tool or the `CXX` environment
variable, and for reliable module builds you should set them explicitly.

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
    codegen_project_root = "/path/to/gentest",
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
export GENTEST_CODEGEN=/opt/sdk/host-tools/bin/gentest_codegen
export CC=/opt/sdk/host-llvm/bin/clang
export CXX=/opt/sdk/host-llvm/bin/clang++

xmake f -c -y \
  --toolchain=llvm \
  --cc="$CC" \
  --cxx="$CXX"
xmake b my_tests
```

On macOS, if Homebrew LLVM or another non-default LLVM prefix is used for the
module lane, also pass `--sdk=<llvm-prefix>` so Xmake does not fall back to the
Apple compiler/toolchain path.

Notes:

- `GENTEST_CODEGEN` is the cleanest explicit route. If it points into a CMake
  build tree, the helper can also reuse the adjacent `compile_commands.json`.
- Module consumers should treat explicit Clang selection as required, not
  best-effort.
- For a future official xrepo package, the package should expose the runtime,
  the helper layer, and a host `gentest_codegen` tool path. It should not try to
  install a host Clang toolchain on the user's behalf.

## Meson

The current Meson support is repo-local and textual-only. Named-module targets
are intentionally unsupported today, so Meson is only a fit for classic or
textual flows right now.

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
  -Dcodegen_path=/opt/sdk/host-tools/bin/gentest_codegen
meson compile -C build/meson
meson test -C build/meson --print-errorlogs
```

Notes:

- `-Dcodegen_path=...` is the explicit host-tool hook.
- Meson named-module support should not be documented as supported until the
  backend grows a real reusable helper API and a proven module lane.

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
