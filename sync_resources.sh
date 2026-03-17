#!/bin/bash
# Syncs project resources/ into the build folder
# Lives in build/ — SCRIPT_DIR is the build directory.

BUILD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$BUILD_DIR/.." && pwd)"
SRC="$PROJECT_DIR/resources/"
DST="$BUILD_DIR/resources/"

if [ ! -d "$DST" ]; then
    echo "Build resources directory not found: $DST"
    echo "Run make first to create the initial build."
    exit 1
fi

rsync -av --checksum "$SRC" "$DST"
echo "Sync complete: $SRC -> $DST"
