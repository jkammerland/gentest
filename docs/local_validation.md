# Private local validation

`scripts/validate_local.py` validates a commit on your own machines. On each
operating system it runs every native validation stage and keeps the reports,
logs and tool versions in a private directory outside your checkout. It
uploads nothing and never contacts other machines.

## Run the validation

You need Python 3.11 or newer, LLVM 23, and a checkout whose `HEAD` is the
commit you validate, without tracked changes. Untracked files are fine; they
aren't copied into the validation. The baseline commit must exist in the same
local Git repository. Then run:

```bash
python3 scripts/validate_local.py run --commit <candidate-SHA> --base <base-SHA> --output <new-private-directory>
```

The command:

- refuses an output directory that already exists or lies inside the checkout;
- creates detached worktrees of the candidate and the baseline in the output
  directory (`source/` and `base/`), fresh build directories under `work/`, and
  logs and JUnit reports under `evidence/`;
- writes `report.json` and `summary.md`, and updates both after every stage;
- exits non-zero unless every required stage passes.

On POSIX systems only you can read the output. On Windows it inherits the
permissions of its parent directory, so choose a private parent, and keep the
path short: Ninja can reject object paths over 260 characters in deeply nested
build directories. Don't put the output directory in an artifact upload, a Git
repository, a shared folder or a pull request.

| Option | Default | Meaning |
| --- | --- | --- |
| `--jobs N` | 2 | Parallel build and test jobs. Nested helper builds use one job. |
| `--timeout S` | 7200 | Time limit for each command, in seconds. CTest's own test timeouts also apply. |
| `--stage NAME` | every stage | Run only this stage; repeat it to run several. The others are reported as INCOMPLETE. |

## Results

Each stage ends as PASS, FAIL or INCOMPLETE. The run as a whole is FAIL if any
stage failed, PASS if every stage passed, and INCOMPLETE otherwise.

- A stage is INCOMPLETE when a required tool or dependency prefix is missing,
  when a stage it depends on didn't pass, when less than 5 GiB of disk space is
  free, or when `--stage` didn't select it.
- Within a stage, the first failing command stops the stage. Stages that depend
  on it become INCOMPLETE; independent stages continue.
- A stage that runs CTest also fails when its JUnit report contains failures or
  when every test was skipped.
- Skipped tests and their reasons are listed in `report.json`. Read them before
  you rely on a passing run.

Every stage first checks the LLVM 23 selection with
`scripts/llvm_tools.py check-toolchain`. GCC, MSVC and AppleClang stages test
those compilers in addition to the LLVM ones.

## Stages

| Host | Stages |
| --- | --- |
| Linux | `clang-debug`, `clang-release`, `gcc-release`, `asan-ubsan`, `tsan`, `coverage`, `package`, `host-codegen`, `aarch64-qemu`, `riscv64-qemu`, `bazel`, `meson`, `xmake`, `recording`, `measured`, `lint` |
| Windows | `clang-relwithdebinfo`, `msvc-relwithdebinfo` |
| macOS | `appleclang-debug`, `appleclang-release`, `llvm-debug`, `llvm-release` |

- The native build stages configure a system preset (`debug-system`,
  `release-system`, `alusan-system`, `tsan-system`, `coverage-system`) with
  `-DGENTEST_ENABLE_PUBLIC_MODULES=AUTO`, so the public modules are on when the
  toolchain supports them. They build everything and run the complete
  configured CTest inventory. The Windows stages use `release-system` with
  `CMAKE_BUILD_TYPE=RelWithDebInfo`.
- `coverage` runs its tests serially, then runs `scripts/coverage_hygiene.py`
  and `scripts/coverage_report.py` with the LLVM 23 `llvm-cov`.
- `package` builds and tests the `release-package` preset, then runs CPack.
- `host-codegen` builds the `gentest_codegen` that the two QEMU stages, `meson`
  and `xmake` use.
- `aarch64-qemu` and `riscv64-qemu` cross-build everything and run the
  complete `aarch64-qemu-full` and `riscv64-qemu-full` test presets.
- `bazel`, `meson`, `xmake` and `recording` run the same commands as CI, from
  `scripts/validation_suites.py`.
- `measured` builds the baseline's benchmark tests and compares their measured
  reports with the candidate's from `clang-release`, using
  `scripts/ci_measured_report_compare.sh`. Regressions above the threshold (10%
  unless `GENTEST_MEASURED_FAIL_REGRESSION_PCT` sets another) are reported
  without failing the stage; a comparison that is malformed or can't run fails
  it.
- `lint` runs `scripts/check_clang_format.sh` and runs
  `scripts/check_clang_tidy.sh` on the `clang-debug` build.

## Tools and dependencies

The script uses the tools already installed on your machine; it doesn't install
toolchains. It never turns off leak detection and never retries tests. Build
tools may download ordinary project dependencies as usual.

- All hosts: Git, Python 3.11 or newer, Ninja, CMake 3.31 or newer, CTest, an
  LLVM/Clang development installation, and fmt. The install guides for
  [Linux](install/linux.md), [macOS](install/macos.md) and
  [Windows](install/windows.md) list the packages.
- Linux: Clang and GCC, clang-format, clang-tidy, `llvm-cov`, gcovr 8.6,
  Bazel, Meson, Xmake, the `aarch64-linux-gnu` and `riscv64-linux-gnu` GCC
  cross toolchains with their sysroots, and `qemu-aarch64` and `qemu-riscv64`.
  The `package` stage needs a vcpkg checkout in `VCPKG_ROOT`, and its
  experimental Common Package Specification (CPS) and software bill of
  materials (SBOM) settings need the CMake 4.3 series; `GENTEST_PACKAGE_CMAKE`
  can point at such a CMake installed next to your main one, for example with
  pip. The `recording` stage needs Glaze and cbor_tags installed under
  `GENTEST_SERIALIZER_PREFIX`.
- Windows: start in an x64 Visual Studio developer environment, such as a
  Developer PowerShell, so that `cl`, the Windows SDK, headers and libraries
  are found. Keep the LLVM development packages findable for both the Clang
  and MSVC stages.
- macOS: make Homebrew LLVM available separately from AppleClang, for example
  with `GENTEST_LLVM_PREFIX`. The `llvm-*` stages refuse to run AppleClang a
  second time.

Environment variables you can set:

| Variable | Meaning |
| --- | --- |
| `GENTEST_LLVM_PREFIX` | Prefix of the host LLVM 23 installation |
| `GENTEST_CLANG`, `GENTEST_CLANGXX` | Host LLVM 23 `clang` and `clang++`. Without them, `clang-23` and `clang++-23` take precedence over unversioned names on `PATH`. |
| `CLANG_FORMAT_BIN`, `CLANG_TIDY_BIN`, `GENTEST_LLVM_COV`, `GENTEST_LLVM_CONFIG`, `GENTEST_CODEGEN_CLANG_SCAN_DEPS` | Individual LLVM 23 tools |
| `GENTEST_CODEGEN_HOST_CLANG` | Host Clang that `gentest_codegen` runs |
| `LLVM_DIR`, `Clang_DIR` | LLVM and Clang CMake package directories |
| `GENTEST_GCC`, `GENTEST_GXX` | Linux GCC executables (default `gcc`, `g++`) |
| `GENTEST_PACKAGE_CMAKE` | CMake for the `package` stage; its sibling `ctest` and `cpack` are used too |
| `VCPKG_ROOT` | vcpkg checkout for the `release-package` preset |
| `GENTEST_SERIALIZER_PREFIX` | Installed Glaze and cbor_tags prefix, or a CMake prefix list |

The report records each tool's SHA-256 hash and version output, and only the
environment variables each command sets explicitly, never your whole
environment.

## Combine reports from several machines

Copy the report directories to one machine through your own private
transport, keeping the files inside them unchanged. Then run `collect` there:

```bash
python3 scripts/validate_local.py collect --commit <full-candidate-SHA> \
  --reports <linux-report-directory> <windows-report-directory> <macos-report-directory> \
  --output <new-private-summary-directory>
```

`collect` reads only those local files and doesn't run Git, so `--commit` must
be the full lowercase commit SHA. It prints the combined summary; `--output`
also writes it to a private directory, which must not exist yet. Run it from a checkout whose
`validate_local.py`, `validation_suites.py`, `llvm_tools.py` and
`llvm_toolchain.json` match the ones that produced the reports.

The combined result passes only when:

- there is exactly one report each for Linux, Windows and macOS;
- all reports name the same candidate and baseline commits;
- the validator scripts and report schema match;
- each source checkout was verified clean;
- every required stage passed and every command in it succeeded;
- every evidence file matches the size and SHA-256 hash its report records.

Otherwise it is INCOMPLETE and `collect` exits non-zero. This is a local
consistency check, not a GitHub merge check.

## Clean up

The reports, logs and worktrees stay for diagnosis. When you are done with
them, remove the worktrees listed under `owned_worktrees` in `report.json` with
`git worktree remove`, then delete the output directory. The script owns
nothing else: no shared dependency cache and no checkout that existed before
the run.
