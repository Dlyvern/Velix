#!/bin/bash
# build_tools.sh — Builds all standalone tools.
# Lives in build/ — SCRIPT_DIR is the build directory.
#
# Usage (from build/):
#   ./build_tools.sh

set -e

BUILD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$BUILD_DIR/.." && pwd)"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Error: no CMake cache found at $BUILD_DIR/CMakeCache.txt"
    echo "Build the engine first: cmake -B build && cmake --build build"
    exit 1
fi

echo "==> Reconfiguring with VELIX_BUILD_TOOLS=ON ..."
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DVELIX_BUILD_TOOLS=ON

echo "==> Building tools ..."
cmake --build "$BUILD_DIR" \
    --target velix_texture_importer \
    --parallel "$(nproc)"

echo "==> Done. Tools built in: $BUILD_DIR/"
