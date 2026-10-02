# Release packaging

Public releases publish the same source archives for Linux, Windows, and macOS:
`gentest-<version>-source.tar.gz` and `gentest-<version>-source.zip`. They contain
the committed source tree, including headers, runtime and codegen sources,
build-system integration, tests, documentation, and existing third-party license
notices. Consumers build the runtime with their own compiler and target
toolchain. The archives contain no compiled Gentest libraries or codegen tools.

Build `gentest_codegen` from source using an installed compatible LLVM/Clang
development toolchain; LLVM 23 is covered by CI on all three OSes. See the
[Linux](install/linux.md), [Windows](install/windows.md), and
[macOS](install/macos.md) build instructions. Separate portable codegen binaries
per host OS and architecture remain future work in
[issue #136](https://github.com/jkammerland/gentest/issues/136). Any future host
binaries must use LLVM 23 and identify their OS and architecture.

Create source archives from a commit or tag with:

```sh
python3 scripts/package_source_release.py --ref HEAD
```

For a signed release, import the private key and provide its full fingerprint:

```sh
export GPG_SIGNING_KEY=0123456789ABCDEF0123456789ABCDEF01234567
export GENTEST_REQUIRE_PACKAGE_SIGNING=ON
python3 scripts/package_source_release.py --ref HEAD
```

The script reads the version and payload from the selected commit, excluding
uncommitted and untracked files. It verifies matching TGZ/ZIP source payloads
and rejects compiled artifacts. Archives and the standalone
`gentest-<version>-source.manifest.json` receive SHA-256/SHA-512 checksums and,
when signing is enabled, ASCII-armored detached signatures (`.asc`). The
manifest identifies the artifact as `source-archive`, records its commit and
tree, archive digests, and packaging tool versions and executable hashes. The
public key is exported alongside the signed artifacts. Source packaging needs
Git and Python 3.11+, plus GnuPG for signing; it does not configure CMake or
install a compiler.

Configure the GitHub `release` environment from the Unix account that owns the
GPG secret key. The helper accepts one or more GitHub repository URLs and does
not write an exported private key to disk:

```sh
scripts/setup_github_release_environment.sh \
  --key 0123456789ABCDEF0123456789ABCDEF01234567 \
  --prompt-passphrase \
  https://github.com/jkammerland/gentest \
  https://github.com/jkammerland/cbor_tags
```

It creates the environment, stores `GPG_FINGERPRINT` as an environment
variable, and stores `GPG_PRIVATE_KEY` plus an optional `GPG_PASSPHRASE` as
environment secrets. Run it as the Unix user whose GnuPG keyring contains the
release key.

The helper configures credentials only. In the repository's **Settings →
Environments → release**, require a reviewer, disable administrator bypass, and
limit deployments to the protected `master` branch before publishing a release.

`GPG_PASSPHRASE_FILE` may point to a protected passphrase file. Signed builds
produce detached signatures for both source archives and the standalone
manifest, and the public key needed to verify them. The script verifies each
signature before it returns success.

## Publishing a release

`scripts/package_source_release.py` creates and validates local source archives.
Publication is handled by the manually dispatched
`.github/workflows/release.yml` workflow. For a new version:

1. Merge the version and changelog update to `master`. Run the full CI profile
   against that exact commit and wait for it to pass. Set `CI_RUN_ID` to the ID
   printed by `gh run list`:

   ```sh
   gh workflow run ci.yml --ref master -f profile=full -f base_ref=v1.2.0
   gh run list --workflow ci.yml --branch master --limit 5
   : "${CI_RUN_ID:?Set CI_RUN_ID from the list above}"
   gh run watch "$CI_RUN_ID" --exit-status
   RELEASE_COMMIT=$(gh run view "$CI_RUN_ID" --json headSha --jq .headSha)
   ```

2. Dispatch the release workflow with the successful full CI run ID. Its
   protected `release` environment holds the signing key and may require a
   reviewer. The workflow verifies that the CI run passed on the exact current
   `master` commit, signs and pushes the annotated tag using that key, verifies
   the tag and version, then archives that exact commit, checks the signed
   assets, and publishes the immutable release. It can resume a matching tag
   and draft, but will not replace a published release. Set `RELEASE_RUN_ID` to
   the ID printed by `gh run list`:

   ```sh
   : "${CI_RUN_ID:?Set CI_RUN_ID to the successful full CI run ID}"
   gh workflow run release.yml --ref master -f tag=v1.2.1 -f ci_run_id="$CI_RUN_ID"
   gh run list --workflow release.yml --limit 5
   : "${RELEASE_RUN_ID:?Set RELEASE_RUN_ID from the list above}"
   gh run watch "$RELEASE_RUN_ID" --exit-status
   ```

Published versions and their signed tags remain immutable. This policy applies
to future releases; existing LLVM-bound release assets are historical artifacts.

## Local host-package validation

`scripts/package_release.sh` and the `release-package` CMake preset remain
available to test local install/export rules. They build an explicitly named
`llvm<major>-host-developer-kit` containing host-built runtime libraries and
`gentest_codegen`. The release publisher does not use this path.

The local preset requires CMake 4.3 and validates CMake config-package metadata,
Common Package Specification metadata, SPDX 3.0.1 SBOMs, bundled license notices,
and checksummed TGZ/ZIP archives. Its installed
`share/gentest/gentest-release-artifact.json` records the host compiler, LLVM
major, and external LLVM runtime requirement. These host packages remain tied
to the local OS, architecture, ABI, and LLVM version.

## License

Gentest is distributed under the Boost Software License 1.0 (`BSL-1.0`). The
project license and existing third-party license texts are included in the
source archive. Local installed host packages also record them in package metadata.
