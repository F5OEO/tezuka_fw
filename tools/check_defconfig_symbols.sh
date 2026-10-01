#!/usr/bin/env bash
# Regression guard against dead Kconfig symbols.
#
# A line like "BR2_PACKAGE_FOO=y" in a defconfig only warns (never errors)
# if BR2_PACKAGE_FOO isn't a real Kconfig symbol -- Buildroot's
# olddefconfig/syncconfig silently drops it from the generated .config
# instead. This is exactly how issue #414 (libad9361.so.0 missing) went
# undetected for years: BR2_PACKAGE_LIBAD9361_IIO and 7 sibling symbols
# were dead in all 14 defconfigs, inherited from ADI's own Buildroot fork
# but never valid against vanilla Buildroot, with no build failure to
# reveal it.
#
# This script runs "make <defconfig>" for one or more boards and fails if
# any "BR2_*=y" line present in the source defconfig is missing from the
# resulting output/<board>/.config.
#
# Usage:
#   tools/check_defconfig_symbols.sh <board> [board2 ...]
#   tools/check_defconfig_symbols.sh all

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILDROOT_DIR="${SCRIPT_DIR}/buildroot"

if ! command -v jq >/dev/null 2>&1; then
    echo "ERROR: jq required to read boards.json" >&2
    exit 1
fi

if [ ! -d "${BUILDROOT_DIR}" ]; then
    echo "ERROR: buildroot/ not found. Run ./getbuildroot.sh first." >&2
    exit 1
fi

if [ -z "${BR2_EXTERNAL:-}" ]; then
    # shellcheck source=../sourceme.first
    source "${SCRIPT_DIR}/sourceme.first"
fi

declare -A BOARDS=()
while IFS=$'\t' read -r _board _defconfig; do
    BOARDS[$_board]="$_defconfig"
done < <(jq -r '.[] | [.board, .defconfig] | @tsv' "${SCRIPT_DIR}/boards.json")

[ $# -eq 0 ] && { echo "Usage: $0 <board|all> [board2 ...]" >&2; exit 1; }

if [ "$1" = "all" ]; then
    TARGETS=("${!BOARDS[@]}")
else
    TARGETS=("$@")
fi

FAIL=0
for board in "${TARGETS[@]}"; do
    defconfig="${BOARDS[$board]:-}"
    if [ -z "$defconfig" ]; then
        echo "ERROR: unknown board '${board}'" >&2
        FAIL=1
        continue
    fi

    output_dir="${SCRIPT_DIR}/output/${board}"
    echo "=== ${board} (${defconfig}) ==="
    make -C "${BUILDROOT_DIR}" O="${output_dir}" "${defconfig}" >/dev/null

    dead=0
    while read -r sym; do
        if ! grep -q "^${sym}=y$" "${output_dir}/.config"; then
            echo "  DEAD SYMBOL: ${sym}=y (in configs/${defconfig}, absent from generated .config)"
            dead=1
        fi
    done < <(grep -E '^BR2_[A-Z0-9_]+=y$' "${SCRIPT_DIR}/configs/${defconfig}" | cut -d= -f1)

    if [ "$dead" -eq 1 ]; then
        FAIL=1
    else
        echo "  OK: no dead symbols"
    fi
done

exit "$FAIL"
