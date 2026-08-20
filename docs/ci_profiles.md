# CI test profiles

Gentest separates pull-request feedback from exhaustive post-merge validation
without caching producer builds or generated parse state.

## Pull requests

One Linux LLVM 22 debug job runs the complete CTest inventory. Other CMake
matrix jobs build the same authored targets but run only tests carrying the
`ci-compat` label. That label covers the main test executables, public API
checks, representative textual and named-module registration, explicit mocks,
runtime shared-library exports, and module-flag regressions.

The package job still configures, builds, and packages from a clean checkout.
Its `package-pr` workflow limits CTest execution to the `package` label so the
installed-consumer contracts run without repeating the repository's entire
runtime and nested-helper inventory.

Measured-report comparison is path-scoped to implementation, generator,
reporting, benchmark, and build-contract inputs.

## Master and manual runs

Pushes to `master` and manual dispatches keep the exhaustive CMake matrix. They
do not apply the `ci-compat` filter, and the package job uses the complete
`package` workflow.

## Parallelism

Standard local system-Clang presets use four outer CTest jobs and cap nested
helper builds at one job through `GENTEST_HELPER_BUILD_PARALLEL_LEVEL=1`. CI
uses the same nested cap. Xmake helper tests share a CTest resource lock because
their tool-level state is not safe to mutate concurrently.

This design deliberately avoids persistent producer, textual parse, or PCM
caches. Every CI job validates artifacts produced from its exact checkout and
toolchain.

## Validation

Inspect the focused inventory after configuring `debug-system`:

```bash
ctest --test-dir build/debug-system --show-only=json-v1 -L '^ci-compat$'
```

Run the same profile locally with:

```bash
ctest --preset=debug-system --output-on-failure -L '^ci-compat$'
```

Run the complete profile by omitting `-L`.
