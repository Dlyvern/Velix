#ifndef ELIX_IASSET_LOADER_HPP
#define ELIX_IASSET_LOADER_HPP

#include "Core/Macros.hpp"

#include <vector>
#include <string>
#include <memory>

#include "Engine/Mesh.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class IAsset
{
public:
    virtual ~IAsset() = default;
};

class ModelAsset : public IAsset
{
public:
    std::vector<Mesh3D> meshes;
    std::vector<std::string> materialPaths;

    ModelAsset(const std::vector<Mesh3D>& meshes) : meshes(meshes) 
    {

    }
};

class IAssetLoader
{
public:
    virtual const std::vector<std::string> getSupportedFormats() const = 0;
    virtual std::shared_ptr<IAsset> load(const std::string& filePath) = 0;

    virtual bool canLoad(const std::string& extension)
    {
        const auto supportedFormats = getSupportedFormats();

        auto it = std::find_if(supportedFormats.begin(), supportedFormats.end(), [&extension](const auto& format)
        {return format == extension; });

        return it == supportedFormats.end() ? false : true;
    }
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_IASSET_LOADER_HPP