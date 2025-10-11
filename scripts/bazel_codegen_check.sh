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
  "$gen" --check tests/unit/cases.cpp -- -std=c++23 -Iinclude -Itests
elif [[ "$mode" == "invalid" ]]; then
  if "$gen" --check tests/smoke/invalid_attrs.cpp -- -std=c++23 -Iinclude -Itests; then
    echo "expected non-zero exit for invalid attributes" >&2
    exit 1
  fi
else
  echo "unknown mode: $mode" >&2
  exit 2
fi
