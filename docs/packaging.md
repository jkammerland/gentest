# Release packaging

Gentest's release package is a host-specific developer kit. It contains the
three CMake library targets, public headers and optional module interfaces,
`gentest_codegen`, the CMake and Xmake integration helpers, documentation, and
all bundled third-party license notices. LLVM and Clang remain host-toolchain
dependencies and are not copied into the archive.

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

`GPG_PASSPHRASE_FILE` may point to a protected passphrase file. Signed builds
produce detached signatures for both archives, ASCII-armored detached
signatures for both standalone SBOM assets, and the public key needed to verify
them. The script verifies all checksums, signatures, CPS fields, SBOM coverage,
and archive contents before it returns success.

## License status

The repository currently has no declared Gentest project license. Package
metadata therefore uses SPDX `NOASSERTION`; this does not grant redistribution
rights. Third-party license texts are complete and are installed separately.
A repository owner must add an explicit project license before publishing a
generally redistributable release.
