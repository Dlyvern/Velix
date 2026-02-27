#ifndef ELIX_IASSET_LOADER_HPP
#define ELIX_IASSET_LOADER_HPP

#include "Core/Macros.hpp"

#include <vector>
#include <string>
#include <memory>
#include <optional>

#include "Engine/Mesh.hpp"
#include "Engine/Skeleton.hpp"
#include "Engine/Material.hpp"
#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Assets/Asset.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class IAssetLoader
{
public:
    virtual ~IAssetLoader() = default;
    virtual const std::vector<std::string> getSupportedFormats() const = 0;
    virtual std::shared_ptr<IAsset> load(const std::string &filePath) = 0;

    virtual bool canLoad(const std::string &extension)
    {
        const auto supportedFormats = getSupportedFormats();

        auto it = std::find_if(supportedFormats.begin(), supportedFormats.end(), [&extension](const auto &format)
                               { return format == extension; });

        return it == supportedFormats.end() ? false : true;
    }
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_IASSET_LOADER_HPP
