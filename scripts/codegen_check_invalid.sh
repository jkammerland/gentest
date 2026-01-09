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

set +e
"${CODEGEN}" --check "${INPUT}" -- -std=c++20 -I"${INCLUDE_DIR}" -I"${TESTS_DIR}"
status=$?
set -e

if [[ ${status} -eq 0 ]]; then
  echo "Expected gentest_codegen --check to fail for invalid_attrs.cpp, but it succeeded." >&2
  exit 1
fi
