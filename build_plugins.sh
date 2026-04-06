#!/bin/bash
# build_plugins.sh — Builds all engine plugins.
# Lives in build/ — SCRIPT_DIR is the build directory.
#
# Usage (from build/):
#   ./build_plugins.sh

set -e

BUILD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$BUILD_DIR/.." && pwd)"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Error: no CMake cache found at $BUILD_DIR/CMakeCache.txt"
    echo "Build the engine first: cmake -B build && cmake --build build"
    exit 1
fi

echo "==> Reconfiguring with VELIX_BUILD_ENGINE_PLUGINS=ON ..."
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DVELIX_BUILD_ENGINE_PLUGINS=ON

echo "==> Building plugins ..."
cmake --build "$BUILD_DIR" \
    --target TerrainPlugin AdvancedAnimationPlugin \
    --parallel "$(nproc)"

echo "==> Done. Plugins in: $BUILD_DIR/resources/plugins/"
