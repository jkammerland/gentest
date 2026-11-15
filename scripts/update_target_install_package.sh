#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <tag, branch or full commit of target_install_package.cmake>" >&2
  exit 2
fi
ref="$1"
repo_url="https://github.com/jkammerland/target_install_package.cmake.git"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"

vendor_prefix="${repo_root}/third_party/target_install_package"
vendor_cmake_dir="${vendor_prefix}/share/cmake/target_install_package"
vendor_config_version_file="${vendor_cmake_dir}/target_install_packageConfigVersion.cmake"
license_file="${repo_root}/third_party/licenses/target_install_package-MIT.txt"
vendor_license_file="${vendor_prefix}/LICENSE"
metadata_file="${vendor_prefix}/VENDORED_TAG.txt"
readme_file="${vendor_prefix}/README.md"

tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/gentest-target-install-package.XXXXXX")"
trap 'rm -rf "${tmp_root}"' EXIT

src_dir="${tmp_root}/src"
build_dir="${tmp_root}/build"

# Fetching one ref works the same for tags, branches and full commit hashes.
echo "Fetching ${repo_url} at ${ref}"
git init -q "${src_dir}"
git -C "${src_dir}" fetch -q --depth 1 "${repo_url}" "${ref}"
git -C "${src_dir}" checkout -q FETCH_HEAD
commit="$(git -C "${src_dir}" rev-parse HEAD)"

echo "Configuring target_install_package ${ref} (${commit})"
rm -rf "${vendor_prefix}"
mkdir -p "${vendor_prefix}"
cmake -S "${src_dir}" -B "${build_dir}" \
  -DTARGET_INSTALL_PACKAGE_ENABLE_INSTALL=ON \
  -DCMAKE_INSTALL_PREFIX="${vendor_prefix}"

echo "Installing into vendored prefix ${vendor_prefix}"
cmake --install "${build_dir}"

if [[ ! -f "${vendor_cmake_dir}/target_install_packageConfig.cmake" ]]; then
  echo "Vendored install is missing target_install_packageConfig.cmake" >&2
  exit 1
fi

cp "${src_dir}/LICENSE" "${vendor_license_file}"
cp "${src_dir}/LICENSE" "${license_file}"

if [[ -f "${vendor_cmake_dir}/target_install_package-config-version.cmake" ]]; then
  cp "${vendor_cmake_dir}/target_install_package-config-version.cmake" "${vendor_config_version_file}"
fi

version="$(sed -n 's/^set(PACKAGE_VERSION "\(.*\)")$/\1/p' "${vendor_config_version_file}" | head -n 1)"

# The ref, then the exact commit it named when vendored.
cat > "${metadata_file}" <<EOF
${ref}
${commit}
EOF

cat > "${readme_file}" <<EOF
Vendored install tree for target_install_package ${version}, from ${ref} at commit ${commit}.

Source repository:
${repo_url}

Update with:
  scripts/update_target_install_package.sh <tag, branch or full commit>
EOF

echo "Updated vendored target_install_package to ${version} (${ref} at ${commit})"
echo "Config package: ${vendor_cmake_dir}/target_install_packageConfig.cmake"
