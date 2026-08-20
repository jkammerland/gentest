#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
artifact_dir="${1:-${repo_root}/build/release-package/artifacts}"

version="$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "${repo_root}/CMakeLists.txt" | head -n 1)"
if [[ -z "${version}" ]]; then
  echo "Could not read the Gentest project version" >&2
  exit 1
fi

if [[ -e "${artifact_dir}" ]] && find "${artifact_dir}" -mindepth 1 -print -quit | grep -q .; then
  echo "Artifact directory must be empty: ${artifact_dir}" >&2
  exit 1
fi
mkdir -p "${artifact_dir}"

if [[ "${GENTEST_REQUIRE_PACKAGE_SIGNING:-OFF}" == "ON" && -z "${GPG_SIGNING_KEY:-}" ]]; then
  echo "GENTEST_REQUIRE_PACKAGE_SIGNING=ON requires GPG_SIGNING_KEY" >&2
  exit 1
fi

cmake --preset=release-package \
  -DGENTEST_REQUIRE_PACKAGE_SIGNING="${GENTEST_REQUIRE_PACKAGE_SIGNING:-OFF}"
cmake --build --preset=release-package
ctest --preset=release-package --output-on-failure
cpack --preset=release -B "${artifact_dir}"

mapfile -t archives < <(find "${artifact_dir}" -maxdepth 1 -type f \( -name '*.tar.gz' -o -name '*.zip' \) -print | sort)
if [[ "${#archives[@]}" -ne 2 ]]; then
  echo "Expected one TGZ and one ZIP package, found ${#archives[@]}" >&2
  exit 1
fi

for archive in "${archives[@]}"; do
  [[ -s "${archive}.sha256" && -s "${archive}.sha512" ]] || {
    echo "Missing checksums for ${archive}" >&2
    exit 1
  }
  (cd "${artifact_dir}" && sha256sum --check "$(basename "${archive}.sha256")")
  (cd "${artifact_dir}" && sha512sum --check "$(basename "${archive}.sha512")")
  if [[ "${GENTEST_REQUIRE_PACKAGE_SIGNING:-OFF}" == "ON" ]]; then
    [[ -s "${archive}.sig" ]] || {
      echo "Missing detached signature for ${archive}" >&2
      exit 1
    }
    gpg --batch --verify "${archive}.sig" "${archive}"
  fi
done

tgz="$(find "${artifact_dir}" -maxdepth 1 -type f -name '*.tar.gz' -print -quit)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/gentest-package-release.XXXXXX")"
trap 'rm -rf "${work_dir}"' EXIT
tar -xzf "${tgz}" -C "${work_dir}"
python3 "${script_dir}/validate_package_metadata.py" "${work_dir}" --version "${version}"

for sbom_name in gentest gentest-tools; do
  sbom_source="$(find "${work_dir}" -type f -path "*/share/sbom/gentest/${sbom_name}.spdx.json" -print -quit)"
  [[ -n "${sbom_source}" ]] || {
    echo "Missing ${sbom_name} SBOM in ${tgz}" >&2
    exit 1
  }
  sbom_dest="${artifact_dir}/${sbom_name}-${version}.spdx.json"
  cp "${sbom_source}" "${sbom_dest}"
  (cd "${artifact_dir}" && sha256sum "$(basename "${sbom_dest}")" > "$(basename "${sbom_dest}").sha256")
  (cd "${artifact_dir}" && sha512sum "$(basename "${sbom_dest}")" > "$(basename "${sbom_dest}").sha512")
  if [[ -n "${GPG_SIGNING_KEY:-}" ]]; then
    gpg_args=(--batch --yes --armor --detach-sign --local-user "${GPG_SIGNING_KEY}")
    if [[ -n "${GPG_PASSPHRASE_FILE:-}" ]]; then
      gpg_args+=(--pinentry-mode loopback --passphrase-file "${GPG_PASSPHRASE_FILE}")
    fi
    gpg "${gpg_args[@]}" --output "${sbom_dest}.asc" "${sbom_dest}"
    gpg --batch --verify "${sbom_dest}.asc" "${sbom_dest}"
  fi
done

if [[ -n "${GPG_SIGNING_KEY:-}" ]]; then
  gpg --batch --armor --export "${GPG_SIGNING_KEY}" > "${artifact_dir}/gentest-${version}-public-key.asc"
  [[ -s "${artifact_dir}/gentest-${version}-public-key.asc" ]] || {
    echo "Failed to export the release public key" >&2
    exit 1
  }
fi

echo "Validated release artifacts in ${artifact_dir}"
