#include "Editor/EditorResourcesStorage.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/Logger.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

void EditorResourcesStorage::loadNeededResources()
{
    const std::string logoPath = "./resources/textures/VelixFire.tex.elixasset";
    const std::string folderPath = "./resources/textures/folder.tex.elixasset";
    const std::string filePath = "./resources/textures/file.tex.elixasset";
    const std::string velixIconPath = "./resources/textures/VelixV.tex.elixasset";

    const bool logoLoaded = tryLoadTextureResource(logoPath, logoPath);
    const bool folderLoaded = tryLoadTextureResource(folderPath, folderPath);
    const bool fileLoaded = tryLoadTextureResource(filePath, filePath);
    const bool velixIconLoaded = tryLoadTextureResource(velixIconPath, velixIconPath);

    if (!logoLoaded || !folderLoaded || !fileLoaded || !velixIconLoaded)
        VX_EDITOR_ERROR_STREAM("Failed to load one or more editor texture assets.\n");
}

VkDescriptorSet EditorResourcesStorage::getTextureDescriptorSet(const std::string &filePath)
{
    auto findDescriptor = [this](const std::string &key) -> VkDescriptorSet
    {
        const auto iterator = m_textures.find(key);
        if (iterator == m_textures.end())
            return VK_NULL_HANDLE;
        return iterator->second.descriptorSet;
    };

    if (const VkDescriptorSet descriptorSet = findDescriptor(filePath); descriptorSet != VK_NULL_HANDLE)
        return descriptorSet;

    const std::string assetPath = toTextureAssetPath(filePath);
    if (!assetPath.empty())
    {
        if (const VkDescriptorSet descriptorSet = findDescriptor(assetPath); descriptorSet != VK_NULL_HANDLE)
            return descriptorSet;

        if (tryLoadTextureResource(assetPath, assetPath))
        {
            if (const VkDescriptorSet descriptorSet = findDescriptor(assetPath); descriptorSet != VK_NULL_HANDLE)
                return descriptorSet;
        }
    }

    VX_EDITOR_WARNING_STREAM("Failed to resolve editor texture descriptor set for: " << filePath << '\n');
    return VK_NULL_HANDLE;
}

std::string EditorResourcesStorage::toTextureAssetPath(const std::string &path) const
{
    if (path.empty())
        return {};

    std::filesystem::path filesystemPath(path);

    std::string extension = filesystemPath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
                   { return static_cast<char>(std::tolower(character)); });

    if (extension == ".elixasset")
        return filesystemPath.lexically_normal().string();

    if (filesystemPath.has_extension())
        filesystemPath.replace_extension(".tex.elixasset");
    else
        filesystemPath += ".tex.elixasset";

    return filesystemPath.lexically_normal().string();
}

bool EditorResourcesStorage::tryLoadTextureResource(const std::string &assetPath, const std::string &storageKey)
{
    if (assetPath.empty())
        return false;

    const std::string key = storageKey.empty() ? assetPath : storageKey;
    if (m_textures.find(key) != m_textures.end())
        return true;

    auto texture = engine::AssetsLoader::loadTextureGPU(assetPath, VK_FORMAT_R8G8B8A8_SRGB);
    if (!texture)
    {
        VX_EDITOR_WARNING_STREAM("Failed to load editor texture asset: " << assetPath << '\n');
        return false;
    }

    const VkDescriptorSet descriptorSet =
        ImGui_ImplVulkan_AddTexture(texture->vkSampler(), texture->vkImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (descriptorSet == VK_NULL_HANDLE)
    {
        VX_EDITOR_WARNING_STREAM("Failed to create ImGui descriptor set for editor texture asset: " << assetPath << '\n');
        return false;
    }

    m_textures[key] = TextureResource{
        .texture = std::move(texture),
        .descriptorSet = descriptorSet};

    return true;
}

ELIX_NESTED_NAMESPACE_END
