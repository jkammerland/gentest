# Release packaging

Gentest's transitional release package is a host-specific developer kit. It contains the
three CMake library targets, public headers and optional module interfaces,
`gentest_codegen`, the CMake and Xmake integration helpers, documentation, and
all bundled third-party license notices. LLVM and Clang remain host-toolchain
dependencies and are not copied into the archive. The archive filename includes
`llvm<major>-host-developer-kit`; it is not a portable SDK and must not be
redistributed or cached under a platform-independent name.

Each archive installs `share/gentest/gentest-release-artifact.json`. This
machine-readable contract records the host, compiler, LLVM major version,
host-built runtime and code-generator status, and external LLVM runtime
requirement. Package validation rejects metadata that incorrectly claims the
current artifact is portable. Future source SDKs and portable host-codegen
artifacts will use distinct artifact kinds under the same versioned contract.

The `release-package` preset requires CMake 4.3 and enables:

- CMake config-package metadata under `share/cmake/gentest`;
- Common Package Specification metadata under `share/cps/gentest`;
- SPDX 3.0.1 JSON-LD SBOMs for the libraries and code-generator tool under
  `share/sbom/gentest`;
- TGZ and ZIP archives with SHA-256 and SHA-512 checksums.

Run the full checked packaging path with:

```sh
scripts/package_release.sh
```

For a signed release, import the private key and provide its full fingerprint:

```sh
export GPG_SIGNING_KEY=0123456789ABCDEF0123456789ABCDEF01234567
export GENTEST_REQUIRE_PACKAGE_SIGNING=ON
scripts/package_release.sh
```

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
produce detached signatures for both archives, ASCII-armored detached
signatures for the standalone artifact manifest and artifact-scoped runtime and
codegen SBOM assets, and the public key needed to verify them. The script
verifies all checksums, signatures, artifact-contract fields, CPS fields, SBOM
coverage, and archive contents before it returns success.

## License

Gentest is distributed under the Boost Software License 1.0 (`BSL-1.0`). The
project license and all bundled third-party license texts are installed in the
release archive and represented in its package metadata.
