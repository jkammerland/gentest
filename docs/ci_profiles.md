# CI profiles and private validation

Pull requests start CI manually. Pushes to `master` start the routine `pr`
bundle after merge. Both the Actions button and CLI default to `pr`:

```bash
gh workflow run ci.yml --ref <candidate-branch> -f profile=pr
```

Use **Actions → Run CI → Run workflow** to choose a branch. The native button
lives in Actions; a PR can link to [Run CI](https://github.com/jkammerland/gentest/actions/workflows/ci.yml).
A newly introduced dispatch workflow becomes available after it reaches the
repository's default branch. Until then, dispatch the existing `cmake.yml`
(with `profile=pr`), `lint.yml`, and `recording.yml` individually on the same
candidate branch.

## Routine GitHub coverage

The matrix selector runs before creating platform jobs. Unknown profiles,
invalid matrix data, and selector failures fail the planning job. There is no
fallback that silently starts the exhaustive matrix.

| Environment | Configuration |
| --- | --- |
| Ubuntu 25.10, Clang 20 | Debug |
| Ubuntu 24.04, GCC | Release |
| Fedora 43, LLVM 22 | ASan + UBSan |
| Windows, LLVM 22 | Debug, plus the MSVC Debug step |
| macOS, Homebrew LLVM 23 | Debug |

All five build the authored targets and run `ci-smoke` tests. This label covers
core unit tests, default authored suites, public API/script contracts,
discovery, one module-registration/mock integration, and small codegen-driver
regressions. Empty selections fail. Package consumers are disabled in these
five jobs. Lint and recording with both real serializers remain in the routine
bundle.

Exhaustive CMake/package/mock/module/shared-library acceptance, coverage,
QEMU, alternate build systems, and measured comparisons run locally by default.
See [private local validation](local_validation.md) for the single run/report
command. Local evidence is never sent to GitHub by these scripts.

## Explicit full GitHub fallback

```bash
gh workflow run ci.yml --ref <candidate-branch> -f profile=full -f base_ref=<base-commit>
```

`full` selects the retained compatibility matrix (31 Linux, five Windows, eight
macOS jobs), complete configured CTest inventories, the package workflow, lint,
coverage, aarch64/riscv64 QEMU, Bazel/Meson/Xmake, measured comparisons, recording,
and the isolated Clang leak diagnostics. Each suite also has its own manual
entry point. All reusable workflows check out the caller's commit.

The two Clang 20 ASan jobs that failed inside libclang are replaced by isolated,
strict XFAIL probes. Ordinary Clang 20 compatibility and other sanitizer lanes
remain. **The probes do not provide end-to-end Clang 20 sanitizer coverage.**
That coverage gap remains until the relevant upstream fixes are available in
supported packages and the full lanes can be restored. Neither libclang nor
Gentest leak detection is patched or globally disabled by this change.

See the [diagnostic ownership leak](issues/libclang_dependent_diagnostic_leak.md)
and [deduction-failure ownership leak](issues/libclang_deduction_failure_leak.md).
`gh workflow run clang_leaks.yml --ref <candidate-branch>` runs just the probes.
Only logs generated on GitHub runners are uploaded by that workflow.

## Known Windows Debug limit

With `GENTEST_SKIP_WINDOWS_DEBUG_DEATH_TESTS=ON`, the discovery fixture skips
only its direct abort sub-probe in the actual Debug configuration and prints:

> Application may suspend for debugger attachment.

Ordinary discovery, non-aborting death checks, and process-launch failure
checks still run. Release keeps the abort probe. The existing runtime death-test
skips remain unchanged. We do not change debugger settings or work around the
Debug CRT behavior.

## Candidate and merge discipline

Rebase or merge the target into the candidate before validation. Manual CI
tests the selected branch commit, not a synthetic merge. A rebase or code change
requires fresh evidence for the resulting commit. Validate the final integration
candidate once, then merge the stack from bottom to top without rewriting its
validated content. Keep local evidence private; any public check description
should contain only a deliberately reviewed, minimal result summary.

Inspect the routine inventory with:

```bash
ctest --test-dir build/debug-system --show-only=json-v1 -L '^ci-smoke$'
```

Full local runs omit label filters. Nested helper builds are capped at one job;
Xmake helpers retain their CTest resource lock. No persistent producer, textual
parse, or PCM cache is introduced.
