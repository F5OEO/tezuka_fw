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
declare -A ARTIFACT=()   # board -> release-artifact group name (defaults to board)
declare -A VARIANT=()    # board -> variant tag within its artifact group (empty if ungrouped)
while IFS=$'\t' read -r _board _defconfig _artifact _variant; do
    BOARDS[$_board]=$_defconfig
    ARTIFACT[$_board]=$_artifact
    VARIANT[$_board]=$_variant
done < <(jq -r '.[] | [.board, .defconfig, (.artifact // .board), (.variant // "")] | @tsv' "${SCRIPT_DIR}/boards.json")

# Artifact groups with more than one member board (e.g. fishball_mini ->
# fishball_mini_7010 + fishball_mini_7020) get merged into one zip; see
# merge_group() below.
declare -A GROUP_MEMBERS=()  # artifact name -> space-separated member boards
for board in "${!BOARDS[@]}"; do
    GROUP_MEMBERS[${ARTIFACT[$board]}]+="${board} "
done
declare -A MERGE_GROUPS=()  # artifact names that have >1 member (i.e. real groups)
for artifact in "${!GROUP_MEMBERS[@]}"; do
    # shellcheck disable=SC2206
    members=(${GROUP_MEMBERS[$artifact]})
    [ "${#members[@]}" -gt 1 ] && MERGE_GROUPS[$artifact]=1
done

usage() {
    echo "Usage: $0 [OPTIONS] <board|group|all> [board2 ...]"
    echo ""
    echo "Options:"
    echo "  -j N    Parallel make jobs (default: auto)"
    echo "  -c      Clean the board's output directory before building"
    echo ""
    echo "Boards:"
    for board in $(printf '%s\n' "${!BOARDS[@]}" | sort); do
        printf "  %-20s %s\n" "${board}" "${BOARDS[$board]}"
    done
    if [ "${#MERGE_GROUPS[@]}" -gt 0 ]; then
        echo ""
        echo "Groups (build all members, merge flash images into one zip):"
        for artifact in $(printf '%s\n' "${!MERGE_GROUPS[@]}" | sort); do
            printf "  %-20s %s\n" "${artifact}" "${GROUP_MEMBERS[$artifact]}"
        done
    fi
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

# Expand "all" and group names (e.g. fishball_mini) to their member boards.
# REQUESTED_GROUPS tracks which groups were named explicitly, so their
# members get merged into one zip after building (see merge_group() below).
TARGETS=()
REQUESTED_GROUPS=()
for arg in "$@"; do
    if [ "$arg" = "all" ]; then
        TARGETS=("${!BOARDS[@]}")
    elif [ -n "${BOARDS[$arg]+x}" ]; then
        TARGETS+=("$arg")
    elif [ -n "${MERGE_GROUPS[$arg]+x}" ]; then
        # shellcheck disable=SC2206
        TARGETS+=(${GROUP_MEMBERS[$arg]})
        REQUESTED_GROUPS+=("$arg")
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

# Merges a group's member boards' images/flash/ outputs into one
# build/<artifact>.zip, each under a flash/<variant>/ subfolder (member
# zips keep identical filenames like pluto.frm/boot.frm across variants,
# so they can't be flattened into one flash/ dir without colliding).
merge_group() {
    local artifact="$1"
    # shellcheck disable=SC2206
    local members=(${GROUP_MEMBERS[$artifact]})
    local merge_dir="${SCRIPT_DIR}/build/.merge-${artifact}"

    rm -rf "${merge_dir}"
    for board in "${members[@]}"; do
        local flash_src="${SCRIPT_DIR}/output/${board}/images/flash"
        local variant="${VARIANT[$board]:-$board}"
        if [ -d "${flash_src}" ]; then
            mkdir -p "${merge_dir}/flash/${variant}"
            cp -r "${flash_src}/." "${merge_dir}/flash/${variant}/"
        else
            echo "WARNING: ${flash_src} not found for ${board}, skipping in merged ${artifact}.zip"
        fi
    done

    (cd "${merge_dir}" && zip -rq "${SCRIPT_DIR}/build/${artifact}.zip" flash)
    rm -rf "${merge_dir}"
    echo "=== ${artifact} complete: build/${artifact}.zip (merged: ${members[*]}) ==="
}

for board in "${TARGETS[@]}"; do
    build_board "${board}"
done

for artifact in "${REQUESTED_GROUPS[@]}"; do
    merge_group "${artifact}"
done

echo ""
echo "=== All builds complete ==="
ls -lh "${SCRIPT_DIR}/build/"*.zip 2>/dev/null || true
