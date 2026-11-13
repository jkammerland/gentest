#!/usr/bin/env bash
set -euo pipefail

WORKSPACE="${TEST_WORKSPACE:-gentest}"
CODEGEN="${TEST_SRCDIR}/${WORKSPACE}/gen_tools/gentest_codegen"
INPUT="${TEST_SRCDIR}/${WORKSPACE}/tests/smoke/invalid_attrs.cpp"
INCLUDE_DIR="${TEST_SRCDIR}/${WORKSPACE}/include"
TESTS_DIR="${TEST_SRCDIR}/${WORKSPACE}/tests"

if [[ ! -x "${CODEGEN}" ]]; then
  echo "gentest_codegen not found or not executable: ${CODEGEN}" >&2
  exit 1
fi

if [[ ! -f "${INPUT}" ]]; then
  echo "invalid input not found: ${INPUT}" >&2
  exit 1
fi

# A crash or a missing libclang also exits non-zero. Only the validation
# failure itself counts: exit code 1 together with the attribute diagnostic.
expected_status=1
expected_diagnostic="unknown gentest attribute 'unknown'"
stderr_file="$(mktemp "${TEST_TMPDIR:-${TMPDIR:-/tmp}}/gentest_codegen_check_invalid.XXXXXX")"
trap 'rm -f "${stderr_file}"' EXIT

set +e
"${CODEGEN}" --check "${INPUT}" -- -std=c++20 -I"${INCLUDE_DIR}" -I"${TESTS_DIR}" 2> "${stderr_file}"
status=$?
set -e
cat "${stderr_file}" >&2

if [[ ${status} -ne ${expected_status} ]]; then
  echo "Expected gentest_codegen --check to exit ${expected_status} for invalid_attrs.cpp, but it exited ${status}." >&2
  exit 1
fi
if ! grep -F -q -- "${expected_diagnostic}" "${stderr_file}"; then
  echo "gentest_codegen --check failed for invalid_attrs.cpp without the diagnostic: ${expected_diagnostic}" >&2
  exit 1
fi
