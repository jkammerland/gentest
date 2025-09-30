#!/bin/bash

# Script to create a new vcpkg port from templates
# Usage: ./create-port.sh <package-name> [output-dir]

set -e

if [ $# -lt 1 ]; then
    echo "Usage: $0 <package-name> [output-dir]"
    echo "Example: $0 mylib /path/to/vcpkg/ports"
    exit 1
fi

PACKAGE_NAME="$1"
OUTPUT_DIR="${2:-./ports}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEMPLATE_DIR="${SCRIPT_DIR}/../port-templates"

# Validate package name (lowercase, no spaces)
if [[ ! "$PACKAGE_NAME" =~ ^[a-z0-9][a-z0-9-]*$ ]]; then
    echo "Error: Package name must be lowercase with only letters, numbers, and hyphens"
    echo "Must start with letter or number"
    exit 1
fi

PORT_DIR="${OUTPUT_DIR}/${PACKAGE_NAME}"

# Check if port already exists
if [ -d "$PORT_DIR" ]; then
    echo "Error: Port directory already exists: $PORT_DIR"
    exit 1
fi

# Create port directory
mkdir -p "$PORT_DIR"

echo "Creating vcpkg port for: $PACKAGE_NAME"
echo "Output directory: $PORT_DIR"

# Prompt for package information
read -p "Package version [1.0.0]: " VERSION
VERSION=${VERSION:-"1.0.0"}

read -p "Package description: " DESCRIPTION
if [ -z "$DESCRIPTION" ]; then
    echo "Error: Description is required"
    exit 1
fi

read -p "Homepage URL: " HOMEPAGE_URL
read -p "Documentation URL (optional): " DOCUMENTATION_URL
read -p "License [MIT]: " LICENSE
LICENSE=${LICENSE:-"MIT"}

read -p "Source archive URL: " SOURCE_URL
if [ -z "$SOURCE_URL" ]; then
    echo "Error: Source URL is required"
    exit 1
fi

echo "Note: You'll need to calculate the SHA512 hash of the source archive"
echo "You can use: curl -L '$SOURCE_URL' | sha512sum"
read -p "SHA512 hash of source archive: " SHA512_HASH

# Create files from templates
cp "$TEMPLATE_DIR/vcpkg.json.template" "$PORT_DIR/vcpkg.json"
cp "$TEMPLATE_DIR/portfile.cmake.template" "$PORT_DIR/portfile.cmake"
cp "$TEMPLATE_DIR/usage.template" "$PORT_DIR/usage"

# Replace placeholders
for file in "$PORT_DIR/vcpkg.json" "$PORT_DIR/portfile.cmake" "$PORT_DIR/usage"; do
    sed -i "s//$PACKAGE_NAME/g" "$file"
    sed -i "s//$VERSION/g" "$file"
    sed -i "s||$DESCRIPTION|g" "$file"
    sed -i "s||$HOMEPAGE_URL|g" "$file"
    sed -i "s||$DOCUMENTATION_URL|g" "$file"
    sed -i "s//$LICENSE/g" "$file"
    sed -i "s||$SOURCE_URL|g" "$file"
    sed -i "s//$SHA512_HASH/g" "$file"
done

# Remove documentation URL line if empty
if [ -z "$DOCUMENTATION_URL" ]; then
    sed -i '/"documentation":/d' "$PORT_DIR/vcpkg.json"
fi

echo ""
echo "Port created successfully at: $PORT_DIR"
echo ""
echo "Next steps:"
echo "1. Review and customize the generated files"
echo "2. Test the port: vcpkg install $PACKAGE_NAME"
echo "3. Add version info: vcpkg x-add-version $PACKAGE_NAME"
echo "4. Submit a PR to microsoft/vcpkg repository"
echo ""
echo "Files created:"
echo "  - vcpkg.json      (port manifest)"
echo "  - portfile.cmake  (build instructions)"
echo "  - usage           (usage documentation)"