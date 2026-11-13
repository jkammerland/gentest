#!/usr/bin/env bash
set -euo pipefail
gen=${1:-}
mode=${2:-valid}
shift 2 || true
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1

if [[ -z "$gen" ]] || [[ ! -x "$gen" ]]; then
  # Try runfiles path
  if [[ -n "${TEST_SRCDIR:-}" ]] && [[ -x "${TEST_SRCDIR}/_main/gentest_codegen" ]]; then
    gen="${TEST_SRCDIR}/_main/gentest_codegen"
  fi
fi

if [[ ! -x "$gen" ]]; then
  echo "gentest_codegen not built; run: bazel build //:gentest_codegen" >&2
  exit 2
fi

if [[ "$mode" == "valid" ]]; then
  "$gen" --check tests/unit/cases.cpp -- -std=c++20 -Iinclude -Itests
elif [[ "$mode" == "invalid" ]]; then
  # A crash or a missing libclang also exits non-zero. Only the validation
  # failure itself counts: exit code 1 together with the attribute diagnostic.
  expected_status=1
  expected_diagnostic="unknown gentest attribute 'unknown'"
  stderr_file="$(mktemp "${TEST_TMPDIR:-${TMPDIR:-/tmp}}/gentest_codegen_check_invalid.XXXXXX")"
  trap 'rm -f "${stderr_file}"' EXIT
  status=0
  "$gen" --check tests/smoke/invalid_attrs.cpp -- -std=c++20 -Iinclude -Itests 2> "${stderr_file}" || status=$?
  cat "${stderr_file}" >&2
  if [[ ${status} -ne ${expected_status} ]]; then
    echo "expected exit code ${expected_status} for invalid attributes, got ${status}" >&2
    exit 1
  fi
  if ! grep -F -q -- "${expected_diagnostic}" "${stderr_file}"; then
    echo "gentest_codegen failed for invalid attributes without the diagnostic: ${expected_diagnostic}" >&2
    exit 1
  fi
else
  echo "unknown mode: $mode" >&2
  exit 2
fi
