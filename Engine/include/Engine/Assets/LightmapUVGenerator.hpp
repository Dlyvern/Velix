#pragma once

#include "Engine/Mesh.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

// Generates non-overlapping lightmap UVs (UV1) for a CPUMesh using xatlas.
// The mesh's lightmapUVs array is populated on success.
// The vertex and index arrays may be re-indexed by xatlas.
// Should be called on static meshes at import time only.
struct LightmapUVGenerator
{
    static void generate(CPUMesh &mesh);
};

ELIX_NESTED_NAMESPACE_END
