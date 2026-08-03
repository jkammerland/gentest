#!/usr/bin/env bash

# Setup vcpkg for CI runners
# Usage: bash scripts/setup-vcpkg.sh

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
VCPKG_CONFIG="${REPO_ROOT}/vcpkg-configuration.json"
VCPKG_REPOSITORY="${GENTEST_VCPKG_REPOSITORY:-https://github.com/Microsoft/vcpkg.git}"

if [ ! -f "${VCPKG_CONFIG}" ]; then
    echo "Missing vcpkg configuration: ${VCPKG_CONFIG}" >&2
    exit 1
fi

VCPKG_BASELINE="$(python3 - "${VCPKG_CONFIG}" <<'PY'
import json
import pathlib
import sys

config = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
baseline = config.get("default-registry", {}).get("baseline", "")
if not isinstance(baseline, str) or not baseline:
    raise SystemExit("vcpkg-configuration.json has no default-registry baseline")
print(baseline)
PY
)"

# Determine vcpkg directory based on CI environment
if [ -n "${VCPKG_ROOT:-}" ]; then
    VCPKG_DIR="${VCPKG_ROOT}"
elif [ "${GITHUB_ACTIONS:-}" = "true" ]; then
    if [ "${RUNNER_OS:-}" = "Windows" ]; then
        VCPKG_DIR="C:/vcpkg"
    else
        VCPKG_DIR="$HOME/vcpkg"
    fi
elif [ -n "${CI_PROJECT_DIR:-}" ]; then
    # GitLab/Gitea (container)
    VCPKG_DIR="/opt/vcpkg"
else
    VCPKG_DIR="$HOME/vcpkg"
fi

echo "Setting up vcpkg in: $VCPKG_DIR"

# Clone only the pinned baseline.  vcpkg manifests are already pinned, but a
# default-branch clone would still make bootstrap behaviour nondeterministic.
VCPKG_NEW_CHECKOUT=false
if [ ! -d "$VCPKG_DIR" ]; then
    echo "Cloning vcpkg baseline ${VCPKG_BASELINE}..."
    git clone --filter=blob:none --no-checkout "$VCPKG_REPOSITORY" "$VCPKG_DIR"
    VCPKG_NEW_CHECKOUT=true
fi

if [ ! -d "$VCPKG_DIR/.git" ]; then
    echo "vcpkg path is not a Git checkout: $VCPKG_DIR" >&2
    exit 1
fi

VCPKG_HEAD=""
if git -C "$VCPKG_DIR" rev-parse --verify HEAD >/dev/null 2>&1; then
    VCPKG_HEAD="$(git -C "$VCPKG_DIR" rev-parse HEAD)"
fi
VCPKG_CAN_REPIN=false
if [ "${GITHUB_ACTIONS:-}" = "true" ] || [ -n "${CI_PROJECT_DIR:-}" ]; then
    VCPKG_CAN_REPIN=true
fi

if [ "$VCPKG_HEAD" != "$VCPKG_BASELINE" ] && [ "$VCPKG_CAN_REPIN" != "true" ] && [ "$VCPKG_NEW_CHECKOUT" != "true" ]; then
    echo "Existing vcpkg checkout is at ${VCPKG_HEAD}, not the project baseline ${VCPKG_BASELINE}." >&2
    echo "Refusing to change a local checkout; set VCPKG_ROOT to a dedicated directory or check it out manually." >&2
    exit 1
fi

VCPKG_REPINNED=false
if [ "$VCPKG_HEAD" != "$VCPKG_BASELINE" ]; then
    git -C "$VCPKG_DIR" remote set-url origin "$VCPKG_REPOSITORY"
    git -C "$VCPKG_DIR" fetch --depth 1 origin "$VCPKG_BASELINE"
    git -C "$VCPKG_DIR" checkout --detach --quiet FETCH_HEAD
    VCPKG_REPINNED=true
fi

if [ "$(git -C "$VCPKG_DIR" rev-parse HEAD)" != "$VCPKG_BASELINE" ]; then
    echo "vcpkg checkout did not resolve baseline ${VCPKG_BASELINE}" >&2
    exit 1
fi

# Bootstrap vcpkg.
cd "$VCPKG_DIR"
if [ "$VCPKG_REPINNED" = "true" ] || { [ ! -f "vcpkg" ] && [ ! -f "vcpkg.exe" ]; }; then
    echo "Bootstrapping vcpkg..."
    if [ "${RUNNER_OS:-}" = "Windows" ]; then
        ./bootstrap-vcpkg.bat -disableMetrics
    else
        ./bootstrap-vcpkg.sh -disableMetrics
    fi
fi

# Export for GitHub Actions
if [ "${GITHUB_ACTIONS:-}" = "true" ]; then
    echo "VCPKG_ROOT=$VCPKG_DIR" >> "$GITHUB_ENV"
    echo "$VCPKG_DIR" >> "$GITHUB_PATH"
else
    # Export for current shell
    export VCPKG_ROOT="$VCPKG_DIR"
    export PATH="$VCPKG_DIR:$PATH"
fi

echo "vcpkg setup complete!"
echo "VCPKG_ROOT=$VCPKG_DIR"
