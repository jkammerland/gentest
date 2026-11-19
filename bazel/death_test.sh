#!/usr/bin/env bash
set -euo pipefail

# rules_shell's Bash launcher supplies rlocation for directory and manifest runfiles.
supervisor=$(rlocation "$1")
suite=$(rlocation "$2")
option_count=$3
shift 3

exec "$supervisor" --skip-as-pass "${@:1:option_count}" -- "$suite" "${@:option_count+1}"
