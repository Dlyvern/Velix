#!/bin/bash

set -e

PHYSX_BUILD_TYPE="checked"
PHYSX_EXTERNAL_ROOT="$(pwd)/physx"
PHYSX_ROOT="${PHYSX_EXTERNAL_ROOT}/PhysX/physx"
PHYSX_LIB_OUTPUT="${PHYSX_EXTERNAL_ROOT}/lib/linux/${PHYSX_BUILD_TYPE}"

cd "$PHYSX_ROOT/"
echo "[Velix_PhysX_build] Generating build files..."
chmod +x generate_projects.sh
./generate_projects.sh linux-gcc-cpu-only
cd compiler/linux-gcc-cpu-only-checked
make -j$(nproc)
echo "[Velix_PhysX_build] Moving built .a files to ${PHYSX_LIB_OUTPUT}"
mkdir -p "${PHYSX_LIB_OUTPUT}"
find ${PHYSX_ROOT}/bin/linux.x86_64/${PHYSX_BUILD_TYPE} -name "*.a" -exec cp {} "${PHYSX_LIB_OUTPUT}/" \;

echo "[Velix_PhysX_build] PhysX built and installed to ${PHYSX_LIB_OUTPUT}"