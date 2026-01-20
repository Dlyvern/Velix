#ifndef ELIX_EDITOR_RESOURCES_STORAGE_HPP
#define ELIX_EDITOR_RESOURCES_STORAGE_HPP

#include "Core/Macros.hpp"

#include "Engine/Texture.hpp"

#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class EditorResourcesStorage
{
public:
    void loadNeededResources();

    VkDescriptorSet getTextureDescriptorSet(const std::string &filePath);

private:
    struct TextureResource
    {
        engine::Texture::SharedPtr texture{nullptr};
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
    };

    std::unordered_map<std::string, TextureResource> m_textures;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_RESOURCES_STORAGE_HPP