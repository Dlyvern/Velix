#include "Engine/Components/LightmapComponent.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Core/Logger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void LightmapComponent::loadFromPath(const std::string &path, VkDescriptorPool )
{
    if (path.empty())
        return;

    lightmapAssetPath = path;

    auto textureAsset = AssetsLoader::loadTexture(path);
    if (!textureAsset.has_value())
    {
        VX_ENGINE_ERROR_STREAM("[LightmapComponent] Failed to load lightmap texture: " << path << '\n');
        return;
    }

    auto tex = std::make_shared<Texture>();
    if (!tex->createFromMemory(textureAsset->pixels.data(),
                               textureAsset->pixels.size(),
                               textureAsset->width,
                               textureAsset->height,
                               static_cast<VkFormat>(textureAsset->vkFormat),
                               textureAsset->channels,
                               textureAsset->mipChain.empty() ? nullptr : &textureAsset->mipChain))
    {
        VX_ENGINE_ERROR_STREAM("[LightmapComponent] Failed to upload lightmap to GPU: " << path << '\n');
        return;
    }

    lightmapTexture = std::move(tex);
    VX_ENGINE_INFO_STREAM("[LightmapComponent] Loaded lightmap: " << path << '\n');
}

ELIX_NESTED_NAMESPACE_END
