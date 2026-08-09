# Codegen timing sidecar

`gentest_codegen --timing-json <path>` writes a JSON sidecar after a successful
invocation. It is intended for build diagnostics, not benchmark reporting.
The sidecar is atomically replaced only when its contents change, and a failed
sidecar write makes the command fail without replacing an existing timing file.

The current schema is `gentest.codegen.timing.v1`
([schema](schemas/gentest.codegen.timing.v1.schema.json)). It contains:

```json
{
  "schema": "gentest.codegen.timing.v1",
  "duration_unit": "microseconds",
  "phases": [
    { "name": "parse", "duration_us": 1234, "tu_index": 0, "source": ".../tu_0000.gentest.cpp" }
  ]
}
```

`phases` reports `startup`, `compdb`, `scan-deps`, `pcm`, `parse`, `merge`,
`mock`, `emit`, and `depfile` for normal generation. TU-wrapper mode reports
one identified `parse` record per input TU; the legacy aggregate parse path
uses a single `parse` record without `tu_index` because Clang handles its input
sources in one invocation. Records may include `source`, `tu_index`, `path`,
and `module` when the phase has a relevant identity. A TU-wrapper `parse`
record may additionally include `cache`: `hit`, `miss`, `disabled`, or
`bypass` (an intentionally ineligible named-module/module-consuming TU, or a
TU using an unsupported cache input such as a VFS overlay, plugin, PCH,
relative forced input, volatile time macro, or preprocessor construct whose
lookup cannot be safely reconstructed). Driver config expansion remains
cacheable because the effective cc1 command is part of the cache context.
Existing v1 consumers can ignore this optional field. A phase can have multiple
records (for example, parallel TU parsing). Named-module precompilation reports
identified `pcm` records only; the otherwise empty PCM stage reports one
unidentified `pcm` record, so phase durations remain additive rather than
counting module work in both child and aggregate records. `emit` covers
registration headers,
wrappers, artifact manifests, and writes of combined registration units. It
excludes the measured mock-specific API-include and attachment-render spans in
same-module registration units. Their union is emitted as one `mock` record,
with `path` and `module` retained when every span belongs to the same
registration unit. This keeps overlapping parallel renders additive with the
corresponding `emit` record. The combined-unit write stays in `emit` because it
is not separable from its registration content. `mock` also covers
mock-manifest/aggregate setup and standalone mock render/write work, so a
mock-bearing invocation can have more than one `mock` record.
Durations use a monotonic process-local clock; they are not reproducible
artifacts and must be excluded from byte-for-byte generated output comparisons.

The timing file is published only on success. It is intentionally not added to
CMake-generated output lists or depfiles, so opting in cannot invalidate or
alter registration headers, manifests, mock output, or depfile content.
Before inspection or generated output publication, its path is checked against
input sources, `--source-root`, the running `gentest_codegen` executable,
auxiliary mock manifests/tools, the active
`compile_commands.json`, and every planned generated artifact. Normal generation
also checks the dependencies discovered during parsing immediately before output
emission, plus the resolved host compiler, scan-deps tool, and generated module
PCM paths. A named-module PCM collision is rejected as soon as its output path is planned,
before precompilation can replace it; external PCM paths are checked as soon as
they are resolved. Precompile-time checks also reserve each PCM's temporary and
compiler fallback `.pcm`/`.ifc` names before they can be removed or written. A
collision is an error, including for `--inspect-source`.
At publication an existing destination must resolve to a regular file. This
rejects directories, FIFOs/devices, and symlinks to non-regular or missing
targets rather than attempting a potentially blocking or destructive write.
As with other generated build outputs, the path validation and atomic writer
assume a trusted build directory; they are not a security boundary against a
concurrent adversary changing the destination between those operations.

The protected tool paths include the codegen executable, the selected host
compiler, explicitly configured scan-deps executables (even when scanning is
disabled), scan-deps executables actually used during scanning, module-precompile
compiler drivers, and resource-directory probe compilers. On macOS, the resolved
`xcrun` helper is reserved before an SDK probe when `SDKROOT` is absent.
Inspect-mode include-directory paths are also reserved. This does not enumerate
arbitrary, non-executed compiler drivers embedded in compilation-database
commands; those commands are treated as trusted build inputs, consistent with
the existing compilation-database trust boundary.
