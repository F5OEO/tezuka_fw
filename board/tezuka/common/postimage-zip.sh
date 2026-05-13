#!/bin/sh
set -e

COMMON_DIR="$(dirname "$0")"
BIN_DIR="$1"

# ── Package ───────────────────────────────────────────────────────────────────

cd "$BIN_DIR" && zip -r tezuka.zip sdimg/* flash/*
