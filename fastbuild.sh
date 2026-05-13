#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILDROOT_DIR="${SCRIPT_DIR}/buildroot"

# Board name -> defconfig mapping, loaded from boards.json (single
# source of truth shared with .github/workflows/main.yml).
if ! command -v jq >/dev/null 2>&1; then
    echo "ERROR: jq required to read boards.json" >&2
    exit 1
fi
declare -A BOARDS=()
while IFS=$'\t' read -r _board _defconfig; do
    BOARDS[$_board]=$_defconfig
done < <(jq -r '.[] | [.board, .defconfig] | @tsv' "${SCRIPT_DIR}/boards.json")

usage() {
    echo "Usage: $0 [OPTIONS] <board|all> [board2 ...]"
    echo ""
    echo "Options:"
    echo "  -j N    Parallel make jobs (default: auto)"
    echo "  -c      Clean the board's output directory before building"
    echo ""
    echo "Boards:"
    while IFS=$'\t' read -r _board _defconfig; do
        printf "  %-20s %s\n" "${_board}" "${_defconfig}"
    done < <(jq -r '.[] | [.board, .defconfig] | @tsv' "${SCRIPT_DIR}/boards.json" | sort)
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
        while IFS=$'\t' read -r _board _; do
            TARGETS+=("$_board")
        done < <(jq -r '.[] | [.board, .defconfig] | @tsv' "${SCRIPT_DIR}/boards.json")
    elif [ -n "${BOARDS[$arg]+x}" ]; then
        TARGETS+=("$arg")
    else
        echo "ERROR: Unknown board '${arg}'"
        echo ""
        usage
    fi
done

OUTPUT_DIR="${SCRIPT_DIR}/output/common"

if [ "${CLEAN}" = true ] && [ -d "${OUTPUT_DIR}" ]; then
        echo "    Cleaning ${OUTPUT_DIR}..."
        if command -v trash >/dev/null 2>&1; then
            trash "${OUTPUT_DIR}"
        else
            rm -rf "${OUTPUT_DIR}"
        fi
    fi

build_board() {
    local board="$1"
    local defconfig="${BOARDS[$board]}"

    echo "=== Building ${board} (${defconfig}) ==="
    echo "    Output: ${OUTPUT_DIR}"

    

    make -C "${BUILDROOT_DIR}" O="${OUTPUT_DIR}" "${defconfig}"
    # shellcheck disable=SC2086
    make -C "${BUILDROOT_DIR}" uboot-reconfigure O="${OUTPUT_DIR}" ${JOBS}
    make -C "${BUILDROOT_DIR}" linux-reconfigure O="${OUTPUT_DIR}" ${JOBS}
    if [[ "${board}" != *mini* ]]; then
        make -C "${BUILDROOT_DIR}" maia-kmod-reconfigure O="${OUTPUT_DIR}" ${JOBS}
    fi
    make -C "${BUILDROOT_DIR}" O="${OUTPUT_DIR}" ${JOBS}
    local zip="${OUTPUT_DIR}/images/tezuka.zip"
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
