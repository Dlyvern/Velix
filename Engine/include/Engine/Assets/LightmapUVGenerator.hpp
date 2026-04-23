#pragma once

#include "Engine/Mesh.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)





struct LightmapUVGenerator
{
    static void generate(CPUMesh &mesh);
};

ELIX_NESTED_NAMESPACE_END
