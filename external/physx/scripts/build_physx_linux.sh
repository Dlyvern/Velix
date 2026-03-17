#!/bin/bash

# Note: no set -e — snippet linking failures are non-fatal (they need OpenGL, we don't)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PHYSX_EXTERNAL_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PHYSX_ROOT="${PHYSX_EXTERNAL_ROOT}/PhysX/physx"
PHYSX_BUILD_TYPE="${1:-checked}"
PHYSX_LIB_OUTPUT="${PHYSX_EXTERNAL_ROOT}/lib/linux/${PHYSX_BUILD_TYPE}"

case "${PHYSX_BUILD_TYPE}" in
    debug|checked|profile|release)
        ;;
    *)
        echo "[Velix_PhysX_build] ERROR: Unsupported PhysX build type '${PHYSX_BUILD_TYPE}'."
        exit 1
        ;;
esac

cd "${PHYSX_ROOT}" || exit 1
echo "[Velix_PhysX_build] Generating build files for ${PHYSX_BUILD_TYPE}..."
chmod +x generate_projects.sh

build_and_collect() {
    local compiler_dir="$1"

    cd "${PHYSX_ROOT}/${compiler_dir}" || exit 1
    # -k: keep going past snippet link failures (snippets require OpenGL, we only need .a libs)
    make -j"$(nproc)" -k 2>&1

    if ! find "${PHYSX_ROOT}/bin/linux.x86_64/${PHYSX_BUILD_TYPE}" -name "libPhysXFoundation*.a" 2>/dev/null | grep -q .; then
        echo "[Velix_PhysX_build] ERROR: PhysX static libraries were not produced for '${PHYSX_BUILD_TYPE}'!"
        exit 1
    fi

    echo "[Velix_PhysX_build] Moving built .a files to ${PHYSX_LIB_OUTPUT}"
    mkdir -p "${PHYSX_LIB_OUTPUT}"
    find "${PHYSX_ROOT}/bin/linux.x86_64/${PHYSX_BUILD_TYPE}" -name "*.a" -exec cp {} "${PHYSX_LIB_OUTPUT}/" \;
}

if command -v nvcc &>/dev/null; then
    CUDA_VERSION=$(nvcc --version 2>/dev/null | grep -oP 'release \K[0-9]+\.[0-9]+' | head -1)
    echo "[Velix_PhysX_build] CUDA found (${CUDA_VERSION}), building with GPU support..."
    ./generate_projects.sh linux-gcc || exit 1
    build_and_collect "compiler/linux-gcc-${PHYSX_BUILD_TYPE}"
    find "${PHYSX_ROOT}/bin/linux.x86_64/${PHYSX_BUILD_TYPE}" -name "libPhysXGpu*.so" -exec cp {} "${PHYSX_LIB_OUTPUT}/" \;
    echo "[Velix_PhysX_build] GPU PhysX built and installed to ${PHYSX_LIB_OUTPUT}"
else
    echo "[Velix_PhysX_build] CUDA not found, building CPU-only..."
    ./generate_projects.sh linux-gcc-cpu-only || exit 1
    build_and_collect "compiler/linux-gcc-cpu-only-${PHYSX_BUILD_TYPE}"
    echo "[Velix_PhysX_build] CPU-only PhysX built and installed to ${PHYSX_LIB_OUTPUT}"
fi
