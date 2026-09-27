# Private local validation

`scripts/validate_local.py` runs the exhaustive native lanes for its current
operating system. Its reports, logs, paths, and tool provenance stay in a private
directory outside the checkout. It has no upload, PR-comment, status-posting,
remote-execution, or telemetry feature. Hostnames, addresses, SSH aliases, and
local machine inventories are not configuration inputs.

## Run

Use Python 3.11+ and a clean tracked checkout at the commit being validated.
Untracked work is preserved and is not copied into the validation worktrees.
Supply a baseline commit available in the same local Git repository:

```bash
python3 scripts/validate_local.py run --commit <candidate-SHA> --base <base-SHA> \
  --output <new-private-directory>
```

The command creates detached candidate and baseline worktrees in that directory,
fresh build directories, `report.json`, and `summary.md`. Existing directories
are refused. POSIX outputs are restricted to the current user; on Windows choose
a parent directory with appropriate user ACLs. Do not add the output directory
to an artifact upload, Git repository, shared folder, or PR attachment.

`--jobs` defaults to two. Nested helper builds use one job. Each command has a
two-hour bound (`--timeout` changes it); CTest's individual test timeouts remain
in force. Failed stages stop their dependent commands while independent stages
continue. The report is checkpointed after each stage. Failed, interrupted,
missing-capability, and partial runs return nonzero. `--stage <name>` can be
repeated for troubleshooting; omitted required stages remain INCOMPLETE.

| Host | Required stages |
| --- | --- |
| Linux | Clang Debug/Release, GCC Release, ASan/UBSan, TSan, coverage report/gate, release packaging, native host codegen, aarch64/riscv64 QEMU, Bazel/Meson/Xmake acceptance, recording/serializers, measured base/current comparison, format/tidy |
| Windows | Clang and MSVC, each Debug/Release, with full configured CTest/package/module inventories |
| macOS | AppleClang and Homebrew LLVM, each Debug/Release, with full configured CTest/package/module inventories |

Native stages use system presets and full CTest inventories. QEMU uses the
existing cross-platform smoke presets. Recording and alternate build systems
share `validation_suites.py` with GitHub. Measured comparison keeps the existing
report-only performance threshold policy; malformed or failed comparisons fail
the stage. Windows Debug skips the known debugger-attachment abort probe and
records its reason. Other CTest skips and their available reasons remain visible
in JSON; inspect these limitations when deciding whether to merge.

## Installed tools and dependencies

Use normally installed compatible tools and supported package managers. The
runner does not install or copy toolchains. Missing required executables and
dependency prefixes yield INCOMPLETE. Compiler/configure/build/test failures
remain failures; the runner never suppresses leak detection or retries tests.

- All hosts: Git, Python, Ninja, CMake 3.31+, CTest, a compatible LLVM/Clang
  development installation, fmt, and Boost.JSON. Use the repository's normal
  installation guides and package managers. Public modules use `AUTO`, enabling
  the surface when the toolchain supports it.
- Linux: Clang, GCC, clang-format/tidy, llvm-cov, gcovr 8.6, Bazel, Meson, Xmake,
  both cross GCC/sysroot installations, and both QEMU user emulators. Prepare
  normal vcpkg dependencies for packaging and install Glaze/cbor_tags before the
  recording stage. The release-package preset's experimental CPS/SBOM identifiers
  currently require the compatible CMake 4.3 series; a normal pip environment may
  provide it alongside the main CMake installation.
- Windows: start in an x64 Visual Studio developer environment so `cl`, its SDK,
  headers, and libraries are available. Keep the LLVM developer packages
  discoverable for both Clang and MSVC builds.
- macOS: make Homebrew LLVM's prefix available separately from AppleClang. The
  runner refuses to count AppleClang a second time as the LLVM lane.

Private environment overrides, when necessary:

| Variable | Meaning |
| --- | --- |
| `LLVM_DIR`, `Clang_DIR` | Installed LLVM/Clang CMake package directories |
| `GENTEST_CLANG`, `GENTEST_CLANGXX` | Native Clang executables; default `clang`, `clang++` |
| `GENTEST_GCC`, `GENTEST_GXX` | Linux GCC executables; default `gcc`, `g++` |
| `GENTEST_LLVM_PREFIX` | Homebrew LLVM prefix for the macOS LLVM lanes |
| `GENTEST_PACKAGE_CMAKE` | Normally installed packaging-compatible CMake executable; its sibling CTest/CPack are used |
| `VCPKG_ROOT` | Normal vcpkg checkout for the release-package preset |
| `GENTEST_SERIALIZER_PREFIX` | Installed Glaze/cbor_tags prefix or CMake prefix list |

Executable hashes and version output are provenance, not a requirement to retain
historical executable bytes. Build tools may fetch ordinary project dependencies
using their normal package/cache mechanisms. The runner records only explicitly
selected command environments, never dumps the inherited environment.

## Collect locally

Move the report directories between your machines using your own private
transport. Keep their relative evidence paths intact. Then, on any machine:

```bash
python3 scripts/validate_local.py collect --commit <full-candidate-SHA> \
  --reports <linux-report-directory> <windows-report-directory> <macos-report-directory> \
  --output <new-private-summary-directory>
```

`collect` only reads those already-local files and optionally writes a local
summary. It does not contact those machines, invoke Git, resolve refs, or use the
network. It requires exactly one report per operating system, the same candidate
and baseline commits, matching validator/schema versions, clean source
verification, all required stages, successful commands, and intact evidence
hashes. Missing hosts/stages, wrong commits, changed logs, and failed/incomplete
runs produce INCOMPLETE and a nonzero exit. This is a local consistency check,
not a remote attestation or a GitHub merge check.

Reports are retained for diagnosis. After private evidence is retained as needed,
remove only this invocation's listed `owned_worktrees` with `git worktree remove`
and remove its build/output directory. No shared dependency cache or pre-existing
checkout is owned by the validator.
