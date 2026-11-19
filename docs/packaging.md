# Packaging

This page describes what a gentest release contains and how to verify it, the
files and metadata that an install provides for packagers, and the steps
maintainers follow to publish a release.

## What a release contains

Each GitHub release publishes source archives of the release commit's
committed tree, for all platforms. They contain no compiled libraries and no
`gentest_codegen`. You build both with your own toolchain; see the install
guides for [Linux](install/linux.md), [macOS](install/macos.md) and
[Windows](install/windows.md). Building `gentest_codegen` needs the LLVM/Clang
development packages, version 19 or newer; CI builds it with LLVM 23 on all
three systems.

The assets of release `vX.Y.Z`:

| File | Contents |
| --- | --- |
| `gentest-X.Y.Z-source.tar.gz`, `gentest-X.Y.Z-source.zip` | The source tree. Both archives hold the same files. |
| `gentest-X.Y.Z-source.manifest.json` | The release manifest (see below). |
| `<file>.sha256`, `<file>.sha512` | A SHA-256 and a SHA-512 checksum for each archive and for the manifest. |
| `<file>.asc` | An ASCII-armored detached GPG signature for each archive and for the manifest. |
| `gentest-X.Y.Z-public-key.asc` | The public key that made the signatures. |

The manifest is JSON with `"schema": "gentest.release-artifact.v1"` and
`"artifact_kind": "source-archive"`. It records the release commit and its
tree, the SHA-256 and SHA-512 digests of both archives, and the version and
SHA-256 hash of the Git, Python and GnuPG executables that made them.

Each release tag is a signed, annotated Git tag. Published releases and their
tags never change.

## Verify a release

In the directory with the downloaded files, import the key, then check a
signature and a checksum:

```bash
gpg --import gentest-2.0.0-public-key.asc
gpg --verify gentest-2.0.0-source.tar.gz.asc gentest-2.0.0-source.tar.gz
sha256sum --check gentest-2.0.0-source.tar.gz.sha256
```

On macOS, use `shasum -a 256 --check` in place of `sha256sum --check`. Compare
the key's fingerprint, shown by `gpg --show-keys gentest-2.0.0-public-key.asc`,
with one you already trust, for example from an earlier release. In a clone of
the repository, `git verify-tag v2.0.0` checks the tag's signature.

## Files an install provides

An install made with `-Dgentest_INSTALL=ON` contains the libraries, the
headers, the CMake package files, and `bin/gentest_codegen` unless you set
`GENTEST_BUILD_CODEGEN=OFF`. It also contains:

| Path | Contents |
| --- | --- |
| `share/gentest/xmake/` | The [Xmake](buildsystems/xmake.md) helper. |
| `share/doc/gentest/` | `README.md`, the third-party `NOTICE.txt`, and the documentation under `docs/`. |
| `share/licenses/gentest/` | `LICENSE` and, under `third_party/`, the third-party license texts. |

An install doesn't contain Clang or `clang-scan-deps`: the machine that runs
`gentest_codegen` provides them (see
[Host toolchains and cross-compiling](buildsystems/host_toolchain_sysroots.md)).

To package the runtime without the code generator, for example as a separate
package from the host tools or for a cross-compiled target, configure with
`-DGENTEST_BUILD_CODEGEN=OFF`. Configure then prints
`gentest: installing runtime without codegen; consumers must select a host tool`,
and projects that use the install point `GENTEST_CODEGEN_EXECUTABLE` at a
`gentest_codegen` from elsewhere. Cross builds use this setting by default.

### Package metadata

Two options add machine-readable package metadata to the install. Both
require CMake 4.3 or newer.

| Option | Installs |
| --- | --- |
| `GENTEST_ENABLE_CPS=ON` | Common Package Specification (CPS) metadata, `share/cps/gentest/gentest.cps`. It describes the `gentest`, `gentest_main` and `gentest_runtime` components, the fmt dependency and the `BSL-1.0` license. |
| `GENTEST_ENABLE_SBOM=ON` | SPDX 3.0.1 software bills of materials in `share/sbom/gentest/`, one for the libraries and one for the installed host tools. CMake 4.5 includes the build configuration in the filenames, for example `gentest-tools-Release.spdx.json`. |

CMake 4.3 and 4.4 require a version-specific activation value for SBOM generation.
With `GENTEST_ENABLE_CPS=ON`, configure stops unless
`CMAKE_EXPERIMENTAL_EXPORT_PACKAGE_DEPENDENCIES` is set. The `release-package`
preset in `CMakePresets.json` sets it and `CMAKE_EXPERIMENTAL_GENERATE_SBOM` to
the values for CMake 4.3.

## Installed package helper

gentest's install rules use the `target_install_package` CMake helper. The
source tree includes a copy of version 7.5.0 in
`third_party/target_install_package`. Configure finds exactly that copy,
without network access and regardless of a cross build's
`CMAKE_FIND_ROOT_PATH`.

To use a separately installed helper instead, for example when your packaging
rules forbid bundled copies, configure with
`-DGENTEST_USE_INSTALLED_PACKAGE_HELPER=ON`. CMake's normal package search then
looks for `target_install_package` 7.5 or a later 7.x release; add its prefix
to `CMAKE_PREFIX_PATH` if CMake doesn't find it. A cross toolchain file that
restricts package searches to the target's roots also restricts this search.

## Publish a release

This section is for maintainers. Releases publish source archives only.
Prebuilt `gentest_codegen` binaries are tracked in
[issue #136](https://github.com/jkammerland/gentest/issues/136); any that are
added must use LLVM 23 and name their OS and architecture.

### Set up the release environment

The release workflow signs with a GPG key stored in the repository's `release`
environment. Set it up once, as the Unix user whose GnuPG keyring holds the
key:

```bash
scripts/setup_github_release_environment.sh \
  --key 0123456789ABCDEF0123456789ABCDEF01234567 \
  --prompt-passphrase \
  https://github.com/jkammerland/gentest
```

The script needs GnuPG and an authenticated GitHub CLI (`gh`). It creates the
environment, stores the key's fingerprint as the environment variable
`GPG_FINGERPRINT`, and stores `GPG_PRIVATE_KEY` and, when you give a
passphrase, `GPG_PASSPHRASE` as environment secrets. It doesn't write the
exported private key to disk.

- `--key` accepts a fingerprint, key ID or user ID. Without it, the script uses
  `RELEASE_GPG_FINGERPRINT` or the only secret key in the keyring.
- `--dry-run` prints the planned changes without writing them.
- `--environment` changes the environment name from `release`.
- It accepts several repositories, as `owner/name` or as GitHub URLs.

The script sets credentials only. In the repository's
**Settings → Environments → release**, also require a reviewer, disable
administrator bypass, and limit deployments to the protected `master` branch.

### Build a host developer kit

`scripts/package_release.sh` builds, tests and packages a host developer kit
(the runtime and `gentest_codegen` for this machine and LLVM) to test the
install and export rules; releases don't publish these kits. It needs CMake 4.3
(the `release-package` preset's activation values belong to that release),
vcpkg with `VCPKG_ROOT` set, and the LLVM/Clang development packages. It writes
the archives, checksums, manifest and SBOMs to
`build/release-package/artifacts/` (or an empty directory you pass) and
validates them with `scripts/validate_package_metadata.py`.
`cmake --workflow --preset=package` runs the same presets without those checks.

### Build the source archives locally

The release workflow makes the archives with
`scripts/package_source_release.py`. You can run it to check a release
candidate:

```bash
python3 scripts/package_source_release.py --ref HEAD
```

`--ref` takes a commit or tag. The script writes to
`build/source-release/artifacts/`, or to the directory you pass as its first
argument, which must be empty. The script reads the version from the selected
commit and packages its tree in UTC with Git checkout line-ending conversion
disabled. Before signing, it checks each archive’s paths, file contents and
executable bits against the commit and rejects compiled files. It writes
SHA-256 and SHA-512 checksums for both archives and the manifest. It needs
Git and Python 3.11 or newer.

To sign, import the private key and pass its full fingerprint:

```bash
export GPG_SIGNING_KEY=0123456789ABCDEF0123456789ABCDEF01234567
export GENTEST_REQUIRE_PACKAGE_SIGNING=ON
python3 scripts/package_source_release.py --ref HEAD
```

`GENTEST_REQUIRE_PACKAGE_SIGNING=ON` turns a missing key into an error, and
`GPG_PASSPHRASE_FILE` can name a file that holds the key's passphrase. Signing
needs GnuPG. The script verifies each signature before it reports success.

### Release steps

1. Set the version in `CMakeLists.txt` (`project(gentest VERSION ...)`) and in
   the files that repeat it: `vcpkg.json`, `MODULE.bazel`, `BUILD.bazel`
   (`_gentest_version`), `meson.build` and `xmake.lua`. The CTest test
   `gentest_vcpkg_manifest_metadata` fails when they differ. Change the
   release's heading in `CHANGELOG.md` to `## X.Y.Z - YYYY-MM-DD`, and merge
   the change to `master`.

2. Run full CI on that `master` commit (see [Continuous integration](ci_profiles.md)). `base_ref`
   is the branch, tag or commit that the measured-report comparison uses as
   its baseline; it defaults to `master`, so pass the previous release tag:

   ```bash
   version=2.0.0
   previous_tag=v1.3.0
   gh workflow run ci.yml --ref master -f base_ref="$previous_tag"
   gh run list --workflow ci.yml --branch master --limit 5
   ```

   Take the ID of the run you started from the list, and wait for it to pass:

   ```bash
   ci_run_id=1234567890
   gh run watch "$ci_run_id" --exit-status
   ```

3. Dispatch the release workflow with the tag and the CI run ID, then wait for
   it. Don't push to `master` in between: the workflow requires the CI run's
   commit to be the tip of `master`.

   ```bash
   gh workflow run release.yml --ref master -f tag="v$version" -f ci_run_id="$ci_run_id"
   gh run list --workflow release.yml --limit 5
   release_run_id=1234567891
   gh run watch "$release_run_id" --exit-status
   ```

The release workflow runs in the protected `release` environment, so it can
wait for a reviewer. It:

- checks that the CI run is a successful `ci.yml` run, dispatched on `master`
  with the title `CI (full) — master`, for the commit that `master` points to;
- checks that the tag matches the version in `CMakeLists.txt` and that
  `CHANGELOG.md` has a dated heading for it;
- imports the key, compares its fingerprint with `GPG_FINGERPRINT`, and signs a
  test file;
- packages the source once without signing before it creates the tag;
- creates and pushes the signed tag, or reuses a matching tag that already
  exists;
- packages and signs the archives, uploads them to a draft release, checks the
  uploaded names and SHA-256 digests, and publishes the release. GitHub
  generates the release notes.

Running the workflow again resumes a matching tag and draft. It never replaces
a published release.

## License

gentest is licensed under the Boost Software License 1.0 (`BSL-1.0`). The
source archives contain `LICENSE` and the third-party license texts in
`third_party/licenses/`. An install puts them in `share/licenses/gentest/` and
the third-party notice in `share/doc/gentest/NOTICE.txt`. The CPS and SBOM
metadata record the license as `BSL-1.0`.
