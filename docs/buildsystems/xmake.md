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

If no host-Clang setting is supplied, the helper resolves the target C++
compiler when it is Clang, then Clang-valued `CXX`/`CC`, then a Clang program
from `PATH`. The resolved executable is passed explicitly to
`gentest_codegen` and participates in the generation-cache identity. Failure
to resolve a host Clang is a configuration error rather than an ambient,
untracked fallback.

## Incremental generation and optional caches

The textual helper rules are dependency-aware. A successful generation records
the owner source, all generated registration/mock products, the codegen and
Clang tool identities, effective codegen flags, configured dependency metadata,
and headers discovered from the codegen depfile. A subsequent unchanged build
does not invoke `gentest_codegen`, compile, or link merely because generated
files are present. Deleting any recorded generated product is a safe cache
miss and regenerates the complete output set. The build-owned sidecar is
schema-versioned and written after successful generation; unreadable or corrupt
state is treated as a miss.
Snapshot publication is serialized by a build-local file lock, uses a unique
same-directory temporary leaf, and atomically renames the immutable result, so
the cache recheck, codegen process, and snapshot publication have one owner.
Two Xmake processes with different effective identities therefore cannot
snapshot each other's generated outputs or delete each other's temporary state.
Depfile parsing decodes only Make's defined escapes; literal backslashes and
Windows drive paths are preserved.

Top-level Clang response files, config files, and VFS overlay YAML files are
tracked as compiler inputs. Xmake tracks the overlay file itself; it does not
interpret its mappings or perform header discovery. If a response file contains
a nested `@response` reference (or cannot be read), Gentest deliberately does
not reimplement Clang's response-file parser in Xmake: codegen runs, but no
Xmake generation snapshot is published or reused. This conservative behavior
keeps Clang authoritative and prevents a nested response-file edit from being
hidden by a partial snapshot.

The sidecar identity includes the resolved `gentest_root`, helper root, and
configured include directories, plus ambient settings that can change parsing
or cache behavior: `GENTEST_CODEGEN_RESOURCE_DIR`,
`GENTEST_CODEGEN_SCAN_DEPS_MODE`, parse/PCM cache enablement, directories and
salts, `CPATH`, the language-specific include-path variables, `INCLUDE`, and
`SDKROOT`, plus `GENTEST_STRICT_FIXTURE` and
`GENTEST_NO_INCLUDE_SOURCES`. Changing one schedules codegen once (or surfaces
the newly requested validation failure); the next unchanged build is a no-op.

Enable the optional textual parse cache with Xmake configuration:

```bash
xmake f --gentest_codegen_parse_cache=y
xmake f --gentest_codegen_parse_cache=y \
  --gentest_codegen_parse_cache_dir=cache/gentest
```

The default enabled path is
`<builddir>/.gentest_codegen_parse_cache`. A relative directory is resolved
from `<builddir>`, not the source tree. When the option is false (the default),
the helper deliberately emits no parse-cache CLI flag, so an ambient
`GENTEST_CODEGEN_PARSE_CACHE=ON` can still opt in. Enabling the option emits
`--parse-cache-dir`; that explicit directory takes precedence over the
environment directory settings. The same fields are available to a downstream
helper configuration:

```lua
codegen = {
    parse_cache = true,
    parse_cache_dir = "cache/gentest", -- relative to Xmake's builddir
},
```

Named-module consumers can separately opt in to the fully validated PCM cache:

```bash
xmake f --gentest_codegen_pcm_cache=y \
  --gentest_codegen_pcm_cache_dir=cache/gentest-pcm
```

Its default directory is `<builddir>/.gentest_codegen_pcm_cache`; relative
paths are build-directory-relative. The helper emits `--pcm-cache-dir` only
for `kind = "modules"` codegen. A cache hit still validates the current
scan-deps file closure and compiler-consumable PCM, so unavailable or
ambiguous scan-deps data safely falls back to normal precompilation.

Compiler caching is separately opt-in and target-local:

```bash
xmake f --gentest_compiler_cache=xmake
```

`off` is the default and preserves Gentest targets' existing disabled Xmake
`build.ccache` policy. `xmake` enables Xmake's own build-local cache for
Gentest textual targets; it does not select or configure external `ccache` or
`sccache`, and does not modify the developer environment. Xmake disables this
policy for named-module targets. Targets that do not call a Gentest helper are
left unchanged.

## Installed helper layout

The downstream Xmake flow is based on an installed prefix. At minimum, the
consumer needs:

- `bin/gentest_codegen`
- `include/gentest/...`
- the complete `share/gentest/xmake/` helper tree, including
  `gentest.lua`, `scripts/update_codegen_dep_cache.lua`,
  `scripts/codegen_dep_cache_common.lua`, and
  `scripts/run_codegen_with_dep_cache.lua`

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
    scripts/
      update_codegen_dep_cache.lua
      codegen_dep_cache_common.lua
      run_codegen_with_dep_cache.lua
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
gentest prefix in `GENTEST_XREPO_PREFIX` and has copied the complete
`share/gentest/xmake/` tree into `.gentest_support/`. The checked-in CTest
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
- Textual and module codegen write an exact lookup-guard sidecar in addition to
  the ordinary depfile. Clang preprocessing callbacks are the only authority
  for header lookup: Xmake consumes the reported candidates and configured
  include-root state, but never reconstructs headers by combining depfile
  entries with include roots. The depfile supplies positive dependencies; the
  sidecar supplies exact missing/earlier candidates and root existence or
  symlink-target state. A per-target runner holds the cache lock while checking
  the immutable snapshot, invoking codegen, and publishing the replacement.
  If the sidecar is missing, corrupt, incomplete, or contains a lookup that
  cannot be represented safely, no snapshot is published or reused and the
  next build regenerates conservatively.
- Invoke Xmake through the normal target build (`xmake build <target>`). Direct
  source-file builds such as `xmake build --files ...` bypass target preparation
  and therefore are not supported for Gentest-generated targets.
