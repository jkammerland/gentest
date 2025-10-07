#!/usr/bin/env bash
set -euo pipefail
mode=${1:-valid}
shift || true
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 1

gen=./bazel-bin/gentest_codegen
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
