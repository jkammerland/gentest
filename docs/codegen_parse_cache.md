# Textual codegen parse cache

`gentest_codegen` can persist successful parse results for textual TU-wrapper
inputs. It is opt-in; the default remains an uncached parse. Named module
sources and textual sources that consume named-module mappings deliberately
bypass the cache.

Enable a direct invocation with a path:

```sh
gentest_codegen --parse-cache-dir build/.gentest_codegen_parse_cache ...
```

For environment-based opt-in, set `GENTEST_CODEGEN_PARSE_CACHE=ON`. Set
`GENTEST_CODEGEN_PARSE_CACHE_DIR` to select a directory; otherwise the cache
directory is `<compdb>/.gentest_codegen_parse_cache` (or the current directory
without `--compdb`). `GENTEST_CODEGEN_PARSE_CACHE_DIR` by itself does not
enable caching; an empty value uses that same default. Invalid boolean values leave caching disabled and emit one
warning. `GENTEST_CODEGEN_PARSE_CACHE_SALT` is an optional invalidation salt.

The CLI path enables caching and wins over both environment settings. CMake
users can opt in with `-DGENTEST_CODEGEN_PARSE_CACHE=ON`; its optional
`GENTEST_CODEGEN_PARSE_CACHE_DIR` is emitted as `--parse-cache-dir` and so has
the same CLI precedence. With no CMake directory it uses the deterministic
`<build>/.gentest_codegen_parse_cache` path. An ambient environment setting
continues to apply when CMake has not emitted a cache CLI option.

Meson users can opt in with `-Dcodegen_parse_cache=true`; its optional
`-Dcodegen_parse_cache_dir=...` is emitted as `--parse-cache-dir`. An empty
directory uses the Gentest project's build directory and a
`.gentest_codegen_parse_cache` child. Relative directories are resolved from
that build directory. The checked-in downstream wrap fixture exposes the same
policy as `-Dgentest_codegen_parse_cache=true` and
`-Dgentest_codegen_parse_cache_dir=...`. When
`codegen_parse_cache=false`, Meson emits no cache CLI option, so the ambient
`GENTEST_CODEGEN_PARSE_CACHE` policy can still opt in. A Meson-emitted
`--parse-cache-dir` wins over both environment settings.

Xmake users can opt in with `xmake f --gentest_codegen_parse_cache=y` and can
set `--gentest_codegen_parse_cache_dir=<dir>`. An empty directory uses
`<builddir>/.gentest_codegen_parse_cache`; relative directories are resolved
from the Xmake build directory. With the option false (the default), Xmake
emits no cache CLI option, so ambient `GENTEST_CODEGEN_PARSE_CACHE=ON` still
applies. When Xmake is explicitly enabled, its emitted `--parse-cache-dir`
takes precedence over environment directory settings.

Entries contain the complete textual parse result (cases, fixtures, mocks and
successful diagnostics) plus the dependency set. Reuse verifies SHA-256
content hashes and physical file identity, adjusted compile command, Clang and
codegen executable identity, parse-policy flags, the ordered command include
metadata, and the optional salt. The serialized parse result has its own
checksum, so a syntactically valid modified entry is also a miss. The preprocessor records negative lookup
guards for earlier include candidates and `__has_include` probes, so adding a
new shadowing header produces a miss. Header maps, frameworks, and
`include_next`/`__has_include_next`, volatile predefined time macros, embeds,
and module imports conservatively bypass caching. The effective Clang driver
command fingerprints default and explicit config expansion. Commands using VFS
overlays, plugins, PCHs, header modules, profile-guided optimization inputs, or
relative forced includes and macros also bypass. Sysroot-derived include roots
(`-isysroot`, `--sysroot`, and `-iwithsysroot`) and record-layout seed files
bypass as well; Gentest does not implement a second header-discovery algorithm
for them. Absolute direct `-include`/`-imacros` inputs are guarded. Clang 22
provides direct embed callbacks; on Clang 20 and 21 Gentest conservatively
scans every entered source buffer and bypasses a TU containing `#embed` or
`__has_embed`.

Entries are written through a same-directory temporary file and atomic rename.
Unreadable, corrupt, stale, locked, read-only, or concurrently written cache
entries are always ordinary misses; they never fail code generation. Cache
files are implementation data, not build outputs or depfile dependencies.
Dependency and source hashing is streamed and limited to 256 MiB per file;
the codegen executable identity is limited to 1 GiB. Larger inputs safely
disable storage or produce a miss instead of being read into memory.
The cache directory is expected to be build-owned and trusted. Direct entry
symlinks are rejected and reads are size-bounded from one opened file, but the
cache is not a security boundary against an adversary replacing directory
components concurrently.

With `--timing-json`, per-TU `parse` records expose the optional `cache` value
`hit`, `miss`, `disabled`, or `bypass`; see [the timing sidecar
reference](codegen_timing.md).

## Validated named-module PCM cache

Named-module precompiles use a separate, opt-in cache. It is disabled by
default and is not a replacement for the textual parse cache:

```sh
gentest_codegen --pcm-cache-dir build/.gentest_codegen_pcm_cache ...
```

Environment opt-in uses `GENTEST_CODEGEN_PCM_CACHE=ON`, with an optional
`GENTEST_CODEGEN_PCM_CACHE_DIR`; `GENTEST_CODEGEN_PCM_CACHE_SALT` provides an
additional explicit invalidation value. CMake exposes
`-DGENTEST_CODEGEN_PCM_CACHE=ON` and
`-DGENTEST_CODEGEN_PCM_CACHE_DIR=...`. Xmake exposes
`--gentest_codegen_pcm_cache=y` and
`--gentest_codegen_pcm_cache_dir=...`; the Xmake helper emits this option only
for module codegen commands. A non-empty `--pcm-cache-dir` takes precedence
over the environment. CMake/Xmake `OFF` emits no CLI option; it does not scrub
an inherited `GENTEST_CODEGEN_PCM_CACHE=ON`, so projects that require a hard
off state must sanitize that command environment.

The PCM cache is deliberately stricter than the textual cache. Reuse requires
a current successful `clang-scan-deps` `experimental-full` closure, including
the current `file-deps` set and scanner command. The key includes the resolved
compiler content identity and version, resource directory, sysroot, normalized
effective precompile command, ordered scanner command/include metadata, the raw
compiler-visible primary-source spelling, source and dependency content hashes
and write times, scan-deps identity/result, codegen schema and options, and
ordered transitive module cache keys. Preserving the primary-source spelling
keeps observable `__FILE__` values distinct; write times invalidate
`__TIMESTAMP__` even when bytes do not change. An imported artifact named
by an explicit `-fmodule-file=name=path` mapping without a validated-cache key
is retained as a typed external PCM dependency and receives a fresh bounded
content/physical-identity check during every prepare, load, and store pass; its
compiler context is still covered by the importing precompile command. A
`-fprebuilt-module-path` search directory does not identify the selected
artifact and is never enumerated as an external input.
If scan-deps is unavailable, ambiguous, incomplete, or falls back to source
scanning, or an effective command contains `-fprebuilt-module-path`, a VFS
overlay, a PCH input, or a compiler/plugin-loading option, PCM caching is a
`bypass` and normal local precompilation continues. These semantic side inputs
are not assumed to be represented by ordinary `file-deps`. A prebuilt
module search directory does not identify the exact selected PCM mapping, so
it is never assumed cache-safe. Scanner output varies by platform and release;
for example, Clang 21 on Windows can report distinct command records for one
module source, which deliberately selects this safe-bypass path.

After a cold local precompile, Gentest runs a preprocessor-only verification
with the same effective module command. Actual expansion of `__DATE__`,
`__TIME__`, or `__TIMESTAMP__`—including through macro indirection—keeps the
local PCM but bypasses shared publication. A verification failure is also a
safe bypass. Existing validated entries therefore never depend on wall-clock
date/time macro values. Because dependency paths are otherwise relocation
normalized, an actual `__FILE__` expansion outside the primary module source
also keeps the local PCM but bypasses shared publication.

Each entry is an atomically renamed directory containing an immutable PCM and
checked metadata. A hit rechecks the complete current closure and PCM digest,
materializes the checked bytes to an invocation-local PCM path, then validates
that local copy with the current compiler's non-mutating `-module-file-info`.
Corrupt, stale, missing, read-only, symlinked, or racing entries are ordinary
misses. Existing entries are never removed or overwritten. `pcm` timing
records report `disabled`, `bypass`, `miss`, or `hit`; these are observability
fields, not performance gates.

Meson remains textual-parse-cache-only by contract. Bazel module actions do
not accept a mutable PCM-cache directory: Bazel's declared action inputs and
native local/remote action caches preserve hermetic module-consumer behavior.
