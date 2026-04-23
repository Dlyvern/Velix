#pragma once

#include "Core/Macros.hpp"
#include "Engine/Components/ECS.hpp"
#include "Engine/Texture.hpp"

#include <string>
#include <volk.h>
#include <glm/vec2.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)






class LightmapComponent final : public ECS
{
public:


    std::string lightmapAssetPath;


    Texture::SharedPtr lightmapTexture;



    glm::vec2 uvOffset{0.0f, 0.0f};
    glm::vec2 uvScale{1.0f, 1.0f};




    void loadFromPath(const std::string &path, VkDescriptorPool descriptorPool = VK_NULL_HANDLE);

    bool isLoaded() const { return lightmapTexture != nullptr; }

    void onAttach() override {}
    void onDetach() override {}
    void update(float ) override {}
};

ELIX_NESTED_NAMESPACE_END
