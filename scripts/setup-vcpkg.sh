#!/usr/bin/env bash

# Setup vcpkg for CI runners
# Usage: bash scripts/setup-vcpkg.sh

set -euo pipefail

# Determine vcpkg directory based on CI environment
if [ "${GITHUB_ACTIONS:-}" = "true" ]; then
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

# Clone vcpkg if not present
if [ ! -d "$VCPKG_DIR" ]; then
    echo "Cloning vcpkg..."
    git clone --depth 1 https://github.com/Microsoft/vcpkg.git "$VCPKG_DIR"
fi

# Bootstrap vcpkg
cd "$VCPKG_DIR"
if [ ! -f "vcpkg" ] && [ ! -f "vcpkg.exe" ]; then
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

