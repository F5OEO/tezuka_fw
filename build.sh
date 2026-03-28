#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILDROOT_DIR="${SCRIPT_DIR}/buildroot"

# Board name -> defconfig mapping
declare -A BOARDS=(
    [pluto]=pluto_maiasdr_defconfig
    [plutoplus]=plutoplus_maiasdr_defconfig
    [e200]=e200_maiasdr_defconfig
    [e310]=e310_maiasdr_defconfig
    [libre]=libre_maiasdr_defconfig
    [fishball]=fishball_maiasdr_defconfig
    [fishball_mini]=fishball_mini_defconfig
    [fishball7020]=fishball_maiasdr_7020_defconfig
    [fishball_mini_7020]=fishball_mini_7020_defconfig
    [nano]=nano_defconfig
    [signalsdrpro]=signalsdrpro_defconfig
    [plutoskyr2]=plutoskyr2_defconfig
)

usage() {
    echo "Usage: $0 [OPTIONS] <board|all> [board2 ...]"
    echo ""
    echo "Options:"
    echo "  -j N    Parallel make jobs (default: auto)"
    echo "  -c      Clean the board's output directory before building"
    echo ""
    echo "Boards:"
    for board in $(printf '%s\n' "${!BOARDS[@]}" | sort); do
        printf "  %-20s %s\n" "${board}" "${BOARDS[$board]}"
    done
    echo "  all                  Build all boards"
    exit 1
}

JOBS=""
CLEAN=false

while getopts "j:ch" opt; do
    case $opt in
        j) JOBS="-j${OPTARG}" ;;
        c) CLEAN=true ;;
        h) usage ;;
        *) usage ;;
    esac
done
shift $((OPTIND - 1))

[ $# -eq 0 ] && usage

# Source BR2_EXTERNAL if not already set
if [ -z "${BR2_EXTERNAL:-}" ]; then
    # shellcheck source=sourceme.first
    source "${SCRIPT_DIR}/sourceme.first"
fi

# Verify buildroot exists
if [ ! -d "${BUILDROOT_DIR}" ]; then
    echo "ERROR: buildroot/ not found. Run ./getbuildroot.sh first."
    exit 1
fi

# Expand "all" to all boards
TARGETS=()
for arg in "$@"; do
    if [ "$arg" = "all" ]; then
        TARGETS=("${!BOARDS[@]}")
    elif [ -n "${BOARDS[$arg]+x}" ]; then
        TARGETS+=("$arg")
    else
        echo "ERROR: Unknown board '${arg}'"
        echo ""
        usage
    fi
done

build_board() {
    local board="$1"
    local defconfig="${BOARDS[$board]}"
    local output_dir="${SCRIPT_DIR}/output/${board}"

    echo "=== Building ${board} (${defconfig}) ==="
    echo "    Output: ${output_dir}"

    if [ "${CLEAN}" = true ] && [ -d "${output_dir}" ]; then
        echo "    Cleaning ${output_dir}..."
        if command -v trash >/dev/null 2>&1; then
            trash "${output_dir}"
        else
            rm -rf "${output_dir}"
        fi
    fi

    make -C "${BUILDROOT_DIR}" O="${output_dir}" "${defconfig}"
    # shellcheck disable=SC2086
    make -C "${BUILDROOT_DIR}" O="${output_dir}" ${JOBS}

    local zip="${output_dir}/images/tezuka.zip"
    if [ -f "${zip}" ]; then
        mkdir -p "${SCRIPT_DIR}/build"
        cp "${zip}" "${SCRIPT_DIR}/build/${board}.zip"
        echo "=== ${board} complete: build/${board}.zip ==="
    else
        echo "WARNING: ${zip} not found"
    fi
}

for board in "${TARGETS[@]}"; do
    build_board "${board}"
done

echo ""
echo "=== All builds complete ==="
ls -lh "${SCRIPT_DIR}/build/"*.zip 2>/dev/null || true
