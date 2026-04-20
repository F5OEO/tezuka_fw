#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Single source of truth for Buildroot release + tarball checksum,
# consumed by CI too (cat buildroot.version >> "$GITHUB_ENV").
# shellcheck source=buildroot.version
. "${SCRIPT_DIR}/buildroot.version"
BR_DIR="${SCRIPT_DIR}/buildroot"

if [ -d "${BR_DIR}" ]; then
    echo "buildroot/ already exists, skipping download."
    echo "To re-download, remove the buildroot/ directory first."
    exit 0
fi

echo "Downloading Buildroot ${BR_VERSION}..."
TARBALL="$(mktemp)"
trap 'rm -f "${TARBALL}"' EXIT

wget -q -O "${TARBALL}" "https://buildroot.org/downloads/buildroot-${BR_VERSION}.tar.gz"

echo "Verifying SHA256..."
if command -v sha256sum >/dev/null 2>&1; then
    echo "${BR_SHA256}  ${TARBALL}" | sha256sum -c -
elif command -v shasum >/dev/null 2>&1; then
    echo "${BR_SHA256}  ${TARBALL}" | shasum -a 256 -c -
else
    echo "WARNING: No sha256sum or shasum found, skipping verification"
fi

echo "Extracting..."
tar -xzf "${TARBALL}" --one-top-level=buildroot --strip-components=1

echo "Applying patches..."
for patch in "${SCRIPT_DIR}"/patches/buildroot/*.patch; do
    [ -f "${patch}" ] || continue
    echo "  Applying $(basename "${patch}")..."
    patch -d "${BR_DIR}" -p1 < "${patch}"
done

echo "Buildroot ${BR_VERSION} ready."
