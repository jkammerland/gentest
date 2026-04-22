# Xmake

gentest now has an official downstream Xmake/xrepo story based on a staged
install payload plus the helper Lua layer installed under
`share/gentest/xmake/`.

For host Clang, sysroot, and cross-build guidance, see
[host_toolchain_sysroots.md](host_toolchain_sysroots.md).

## Public helper API

After loading `gentest.lua`, the downstream Xmake surface is:

- `gentest_configure({...})`
- `gentest_add_public_modules({...})`
- `gentest_add_mocks({...})`
- `gentest_attach_codegen({...})`

The contract is still explicit 2-step codegen:

1. add mocks
2. attach suite codegen

For module suites, `gentest_attach_codegen({ kind = "modules", ... })` requires
an explicit `module_name`. Xmake keeps the authored `.cppm` in the module build
and adds a generated same-module registration implementation source plus a
`<target>.artifact_manifest.json` product. The helper predeclares those files
for Xmake, then `gentest_codegen` fills and classifies them.

For module consumers, `gentest_add_public_modules({...})` is the shared owner of
the installed `gentest`, `gentest.mock`, and `gentest.bench_util` module files.
Downstream module mock and suite targets can then set
`public_modules_via_deps = true` and depend on that provider target instead of
trying to own the public module names multiple times.

Host-tool configuration is explicit:

```lua
gentest_configure({
    project_root = os.projectdir(),
    gentest_root = "/abs/prefix",
    helper_root = "/abs/prefix/share/gentest/xmake",
    incdirs = {"tests"},
    gentest_common_defines = {"FMT_HEADER_ONLY"},
    gentest_common_cxxflags = {"-Wno-attributes"},
    gentest_module_files = {
        "/abs/prefix/include/gentest/gentest.cppm",
        "/abs/prefix/include/gentest/gentest.mock.cppm",
        "/abs/prefix/include/gentest/gentest.bench_util.cppm",
    },
    codegen = {
        exe = "/abs/prefix/bin/gentest_codegen",
        clang = "/opt/llvm/bin/clang++",
        scan_deps = "/opt/llvm/bin/clang-scan-deps",
    },
})
```

The env fallbacks remain:

- `GENTEST_CODEGEN`
- `GENTEST_CODEGEN_HOST_CLANG`
- `GENTEST_CODEGEN_CLANG_SCAN_DEPS`

## Installed helper layout

The downstream Xmake flow is based on an installed prefix. At minimum, the
consumer needs:

- `bin/gentest_codegen`
- `include/gentest/...`
- `share/gentest/xmake/gentest.lua`

The checked-in downstream proof copies the helper payload into a project-local
directory and loads it like this:

```lua
includes(".gentest_support/gentest.lua")

gentest_configure({
    project_root = os.projectdir(),
    gentest_root = os.getenv("GENTEST_XREPO_PREFIX"),
    helper_root = path.join(os.projectdir(), ".gentest_support"),
    ...
})
```

Minimal consumer layout:

```text
your_project/
  xmake.lua
  .gentest_support/
    gentest.lua
  tests/
    main.cpp
    cases.cpp
    cases.cppm
    header_mock_defs.hpp
    module_mock_defs.cppm
    service.hpp
    service_module.cppm
```

## Downstream xrepo example

```lua
set_project("gentest_xrepo_consumer")
set_languages("cxx20")

add_rules("mode.debug", "mode.release")
add_repositories("local-gentest repo")
add_requires("fmt")
add_requires("gentest")

includes(".gentest_support/gentest.lua")

target("gentest_xrepo_public_modules")
    set_kind("moduleonly")
    add_packages("fmt", "gentest")
    gentest_add_public_modules({
        output_dir = path.join(current_gen_root(), "consumer_public_modules"),
    })

target("gentest_xrepo_module_mocks")
    set_kind("static")
    add_packages("fmt", "gentest")
    add_deps("gentest_xrepo_public_modules")
    gentest_add_mocks({
        name = "gentest_xrepo_module_mocks",
        kind = "modules",
        defs = {"tests/service_module.cppm", "tests/module_mock_defs.cppm"},
        defs_modules = {"downstream.xrepo.service", "downstream.xrepo.mock_defs"},
        module_name = "downstream.xrepo.consumer_mocks",
        output_dir = path.join(current_gen_root(), "consumer_module_mocks"),
        public_modules_via_deps = true,
    })

target("gentest_xrepo_module")
    set_kind("binary")
    add_packages("fmt", "gentest")
    gentest_attach_codegen({
        name = "gentest_xrepo_module",
        kind = "modules",
        module_name = "downstream.xrepo.consumer_cases",
        source = "tests/cases.cppm",
        main = "tests/main.cpp",
        output_dir = path.join(current_gen_root(), "consumer_module"),
        deps = {"gentest_xrepo_public_modules", "gentest_xrepo_module_mocks"},
        public_modules_via_deps = true,
        defines = {"GENTEST_XREPO_USE_MODULES=1"},
    })
```

## Configure and build

These commands assume the downstream project already has an installed/staged
gentest prefix in `GENTEST_XREPO_PREFIX` and has copied
`share/gentest/xmake/gentest.lua` into `.gentest_support/`. The checked-in CTest
proof creates that staged prefix and support copy automatically.

```bash
export GENTEST_XREPO_PREFIX=/abs/prefix
export GENTEST_CODEGEN_HOST_CLANG=/opt/llvm/bin/clang++
export GENTEST_CODEGEN_CLANG_SCAN_DEPS=/opt/llvm/bin/clang-scan-deps

xmake f -c -y -m debug -o build/xmake-downstream \
  --toolchain=llvm \
  --cc=/opt/llvm/bin/clang \
  --cxx=/opt/llvm/bin/clang++
xmake b gentest_xrepo_module
xmake run gentest_xrepo_textual -- --list
xmake run gentest_xrepo_module -- --run=downstream/xrepo/mock --kind=test
```

To run the checked-in proof instead of a prepared downstream project:

```bash
ctest --preset=debug-system --output-on-failure -R '^gentest_xmake_xrepo_consumer$'
```

On Windows, use Xmake's LLVM toolchain explicitly when the module lane is
enabled:

```powershell
$env:GENTEST_XREPO_PREFIX = "C:/gentest"
$env:GENTEST_CODEGEN_HOST_CLANG = "C:/Tools/llvm-21.1.4/bin/clang++.exe"
$env:GENTEST_CODEGEN_CLANG_SCAN_DEPS = "C:/Tools/llvm-21.1.4/bin/clang-scan-deps.exe"

xmake f -c -y -m debug -o build/xmake-downstream `
  --toolchain=llvm `
  --cc=C:/Tools/llvm-21.1.4/bin/clang.exe `
  --cxx=C:/Tools/llvm-21.1.4/bin/clang++.exe
xmake b gentest_xrepo_module
```

## Checked-in proofs

The repository now validates:

- repo-root textual/module helper examples
- downstream xrepo consumer in
  [`tests/downstream/xmake_xrepo_consumer`](../../tests/downstream/xmake_xrepo_consumer)

The downstream proof in
[`tests/cmake/scripts/CheckXmakeXrepoConsumer.cmake`](../../tests/cmake/scripts/CheckXmakeXrepoConsumer.cmake):

- stages a real install prefix with `gentest_codegen`, public headers/modules,
  and `share/gentest/xmake`
- configures a fixture-local xrepo repository that consumes that staged prefix
- builds textual mock target + textual consumer
- builds module public-module provider + module mocks + module consumer
- verifies generated mock/codegen artifacts, including module registration
  sources and artifact manifests
- runs the consumer test/mock/bench/jitter surface

## Validated platforms

CI validates the Xmake downstream path on Ubuntu 24.04 and Fedora 43. Windows
and macOS commands in this guide document the intended native setup; they are
not yet separate non-CMake CI lanes.

## Limitations

- Module consumers still require a Clang target toolchain in Xmake.
- Module suite helpers require callers to provide the authored module name
  explicitly.
- The public-module provider step is required for installed-prefix module users;
  that ownership is not inferred automatically.
- `deps` can carry gentest helper metadata and package metadata, but arbitrary
  Xmake target public include/module settings are not inferred for codegen.
- The current package shape is validated through the checked-in fixture-local
  xrepo repository, not a published external xrepo registry entry yet.
