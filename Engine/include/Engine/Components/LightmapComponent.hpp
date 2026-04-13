#pragma once

#include "Core/Macros.hpp"
#include "Engine/Components/ECS.hpp"
#include "Engine/Texture.hpp"

#include <string>
#include <volk.h>
#include <glm/vec2.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

// Stores a pre-baked lightmap irradiance texture for a static mesh entity.
// Assigned automatically by LightmapBaker after a successful bake.
// At runtime the GBuffer pass samples this texture using the mesh's lightmap UVs
// and outputs the irradiance to the GBuffer loc-5 attachment so the lighting pass
// can skip dynamic shadow map sampling for this entity.
class LightmapComponent final : public ECS
{
public:
    // Path to the .texture.elixasset containing the baked irradiance.
    // Empty = no lightmap loaded yet.
    std::string lightmapAssetPath;

    // Loaded GPU texture. Null until loadFromPath() succeeds.
    Texture::SharedPtr lightmapTexture;

    // Optional UV transform to fit the mesh into a shared atlas in the future.
    // For now these remain (0,0) / (1,1) since each entity has its own texture.
    glm::vec2 uvOffset{0.0f, 0.0f};
    glm::vec2 uvScale{1.0f, 1.0f};

    // Loads the lightmap texture from disk and uploads it to the GPU.
    // descriptorPool is used for bindless registration (may be VK_NULL_HANDLE
    // if the caller registers the texture separately).
    void loadFromPath(const std::string &path, VkDescriptorPool descriptorPool = VK_NULL_HANDLE);

    bool isLoaded() const { return lightmapTexture != nullptr; }

    void onAttach() override {}
    void onDetach() override {}
    void update(float /*deltaTime*/) override {}
};

ELIX_NESTED_NAMESPACE_END
