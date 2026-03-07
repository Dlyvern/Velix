#!/bin/bash

# Note: no set -e — snippet linking failures are non-fatal (they need OpenGL, we don't)

PHYSX_BUILD_TYPE="checked"
PHYSX_EXTERNAL_ROOT="$(pwd)/physx"
PHYSX_ROOT="${PHYSX_EXTERNAL_ROOT}/PhysX/physx"
PHYSX_LIB_OUTPUT="${PHYSX_EXTERNAL_ROOT}/lib/linux/${PHYSX_BUILD_TYPE}"

cd "$PHYSX_ROOT/" || exit 1
echo "[Velix_PhysX_build] Generating build files..."
chmod +x generate_projects.sh

build_and_collect() {
    local compiler_dir="$1"
    # -k: keep going past snippet link failures (snippets require OpenGL, we only need .a libs)
    make -j$(nproc) -k 2>&1
    # Validate that the essential static libs were actually produced
    if ! find "${PHYSX_ROOT}/bin/linux.x86_64/${PHYSX_BUILD_TYPE}" -name "libPhysXFoundation*.a" 2>/dev/null | grep -q .; then
        echo "[Velix_PhysX_build] ERROR: PhysX static libraries were not produced!"
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
    cd compiler/linux-gcc-checked || exit 1
    build_and_collect
    # Copy GPU shared library (dlopen'd at runtime by PhysX)
    find "${PHYSX_ROOT}/bin/linux.x86_64/${PHYSX_BUILD_TYPE}" -name "libPhysXGpu*.so" -exec cp {} "${PHYSX_LIB_OUTPUT}/" \;
    echo "[Velix_PhysX_build] GPU PhysX built and installed to ${PHYSX_LIB_OUTPUT}"
else
    echo "[Velix_PhysX_build] CUDA not found, building CPU-only..."
    ./generate_projects.sh linux-gcc-cpu-only || exit 1
    cd compiler/linux-gcc-cpu-only-checked || exit 1
    build_and_collect
    echo "[Velix_PhysX_build] CPU-only PhysX built and installed to ${PHYSX_LIB_OUTPUT}"
fi
