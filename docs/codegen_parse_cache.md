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
relative forced includes and macros also bypass; absolute direct
`-include`/`-imacros` inputs are guarded. Clang 22
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
