#ifndef ELIX_ASSETS_LOADER_HPP
#define ELIX_ASSETS_LOADER_HPP

#include "Core/Macros.hpp"
#include "Engine/Mesh.hpp"

#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class AssetsLoader
{
public:
    static Mesh3D loadModel(const std::string& path);

};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_ASSETS_LOADER_HPP