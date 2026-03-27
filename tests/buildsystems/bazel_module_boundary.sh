#!/usr/bin/env bash
set -euo pipefail

mode="${1:-}"
label="${2:-}"
target_kind="${3:-named-module target}"

emit_message() {
    printf 'gentest Bazel named-module target %s (%s) is intentionally disabled.\n\n' "${label}" "${target_kind}"
    cat <<'EOF'
Current real blockers on the repo-local Bazel modules path:
- `rules_cc` `module_interfaces` requires `--experimental_cpp_modules`.
- With Bazel 9.0.0 + `rules_cc` 0.2.14 on this repo, GCC-backed `local_config_cc` then fails in `aggregate-ddi` with `Invalid JSON string`.
- With Clang, the repo reaches `gentest_codegen`, but its module mock discovery still fails on the Bazel path while importing generated PCMs.

Use the textual Bazel targets documented in `docs/buildsystems/bazel.md`, or use CMake for named-module support.
EOF
}

case "${mode}" in
    --fail)
        if [[ -z "${label}" ]]; then
            echo "missing Bazel label" >&2
            exit 2
        fi
        emit_message >&2
        exit 1
        ;;
    --check)
        if [[ -z "${label}" ]]; then
            echo "missing Bazel label" >&2
            exit 2
        fi
        output="$(emit_message)"
        [[ "${output}" == *"intentionally disabled"* ]]
        [[ "${output}" == *'--experimental_cpp_modules'* ]]
        [[ "${output}" == *"aggregate-ddi"* ]]
        [[ "${output}" == *"gentest_codegen"* ]]
        [[ "${output}" == *"docs/buildsystems/bazel.md"* ]]
        printf '%s\n' "${output}"
        ;;
    *)
        echo "usage: $0 (--fail|--check) <label> [target-kind]" >&2
        exit 2
        ;;
esac
