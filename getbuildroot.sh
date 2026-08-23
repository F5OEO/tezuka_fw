#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Single source of truth for Buildroot release + tarball checksums
# (primary + GitHub-mirror fallback); CI hashes this file's content into
# its cache keys (see main.yml) so it doesn't need to parse it directly.
# shellcheck source=buildroot.version
. "${SCRIPT_DIR}/buildroot.version"
BR_DIR="${SCRIPT_DIR}/buildroot"

if [ -d "${BR_DIR}" ]; then
    echo "buildroot/ already exists, skipping download."
    echo "To re-download, remove the buildroot/ directory first."
    exit 0
fi

TARBALL="$(mktemp)"
trap 'rm -f "${TARBALL}"' EXIT

PRIMARY_URL="https://buildroot.org/downloads/buildroot-${BR_VERSION}.tar.gz"
# GitHub mirror of gitlab.com/buildroot.org/buildroot -- fallback for when
# buildroot.org itself is unreachable (see buildroot.version). Different
# packaging than the official dist tarball, so it verifies against its own
# pinned checksum below.
FALLBACK_URL="https://github.com/buildroot/buildroot/archive/refs/tags/${BR_VERSION}.tar.gz"

echo "Downloading Buildroot ${BR_VERSION} from buildroot.org..."
if wget -q --timeout=30 --tries=2 -O "${TARBALL}" "${PRIMARY_URL}"; then
    EXPECTED_SHA256="${BR_SHA256}"
else
    echo "buildroot.org unreachable, falling back to the GitHub mirror..."
    if wget -q --timeout=30 --tries=2 -O "${TARBALL}" "${FALLBACK_URL}"; then
        EXPECTED_SHA256="${BR_SHA256_GITHUB}"
    else
        echo "ERROR: could not download Buildroot ${BR_VERSION} from buildroot.org or the GitHub mirror" >&2
        exit 1
    fi
fi

echo "Verifying SHA256..."
if command -v sha256sum >/dev/null 2>&1; then
    echo "${EXPECTED_SHA256}  ${TARBALL}" | sha256sum -c -
elif command -v shasum >/dev/null 2>&1; then
    echo "${EXPECTED_SHA256}  ${TARBALL}" | shasum -a 256 -c -
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
