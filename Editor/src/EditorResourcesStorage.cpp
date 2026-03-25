#include "Editor/EditorResourcesStorage.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/Logger.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include <backends/imgui_impl_vulkan.h>
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

namespace
{
    std::string resolveBundledEditorTextureSourcePath(const std::string &storageKey)
    {
        if (storageKey == "./resources/textures/VelixFire.tex.elixasset")
            return "./resources/textures/VelixFire.png";
        if (storageKey == "./resources/textures/VelixV.tex.elixasset")
            return "./resources/textures/VelixV.png";

        return {};
    }

    engine::Texture::SharedPtr loadTextureDirectlyFromImageSource(const std::string &sourcePath, VkFormat format)
    {
        if (sourcePath.empty())
            return nullptr;

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc *pixels = stbi_load(sourcePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels)
            return nullptr;

        engine::Texture::SharedPtr texture = std::make_shared<engine::Texture>();
        const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
        const bool created = texture->createFromMemory(pixels, byteCount, static_cast<uint32_t>(width), static_cast<uint32_t>(height), format, 4u);
        stbi_image_free(pixels);

        if (!created)
            return nullptr;

        return texture;
    }
}

void EditorResourcesStorage::loadNeededResources(bool backendRecreated)
{
    if (!m_textures.empty() && backendRecreated)
        refreshLoadedTextureDescriptors(backendRecreated);

    const std::string logoPath = "./resources/textures/VelixFire.tex.elixasset";
    const std::string velixIconPath = "./resources/textures/VelixV.tex.elixasset";

    const bool logoLoaded = tryLoadTextureResource(resolveBundledEditorTextureSourcePath(logoPath), logoPath);
    const bool velixIconLoaded = tryLoadTextureResource(resolveBundledEditorTextureSourcePath(velixIconPath), velixIconPath);

    if (!logoLoaded || !velixIconLoaded)
        VX_EDITOR_ERROR_STREAM("Failed to load one or more editor texture assets.\n");
}

void EditorResourcesStorage::refreshLoadedTextureDescriptors(bool backendRecreated)
{
    for (auto &[storageKey, resource] : m_textures)
    {
        if (!resource.texture)
            continue;

        if (resource.descriptorSet != VK_NULL_HANDLE && !backendRecreated)
            ImGui_ImplVulkan_RemoveTexture(resource.descriptorSet);

        resource.descriptorSet = ImGui_ImplVulkan_AddTexture(
            resource.texture->vkSampler(),
            resource.texture->vkImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (resource.descriptorSet == VK_NULL_HANDLE)
            VX_EDITOR_WARNING_STREAM("Failed to refresh ImGui descriptor set for editor texture asset: " << storageKey << '\n');
    }
}

VkDescriptorSet EditorResourcesStorage::getTextureDescriptorSet(const std::string &filePath)
{
    auto tryRefreshEntry = [this](const std::string &key) -> VkDescriptorSet
    {
        auto it = m_textures.find(key);
        if (it == m_textures.end())
            return VK_NULL_HANDLE;

        if (it->second.descriptorSet != VK_NULL_HANDLE)
            return it->second.descriptorSet;

        // Entry exists but descriptor is null — retry AddTexture (e.g. backend wasn't ready during initial load)
        if (it->second.texture)
        {
            it->second.descriptorSet = ImGui_ImplVulkan_AddTexture(
                it->second.texture->vkSampler(),
                it->second.texture->vkImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            return it->second.descriptorSet;
        }

        return VK_NULL_HANDLE;
    };

    // Direct lookup by exact path (handles "./resources/..." keys from loadNeededResources)
    if (const VkDescriptorSet descriptorSet = tryRefreshEntry(filePath); descriptorSet != VK_NULL_HANDLE)
        return descriptorSet;

    const std::string assetPath = toTextureAssetPath(filePath);
    if (!assetPath.empty())
    {
        // Lookup by normalized path (no leading ./)
        if (const VkDescriptorSet descriptorSet = tryRefreshEntry(assetPath); descriptorSet != VK_NULL_HANDLE)
            return descriptorSet;

        // Last resort: try to load on demand
        if (tryLoadTextureResource(assetPath, assetPath))
        {
            if (const VkDescriptorSet descriptorSet = tryRefreshEntry(assetPath); descriptorSet != VK_NULL_HANDLE)
                return descriptorSet;
        }

        const std::string bundledSourcePath = resolveBundledEditorTextureSourcePath(assetPath);
        if (!bundledSourcePath.empty() && tryLoadTextureResource(bundledSourcePath, assetPath))
        {
            if (const VkDescriptorSet descriptorSet = tryRefreshEntry(assetPath); descriptorSet != VK_NULL_HANDLE)
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
    auto existingIt = m_textures.find(key);
    if (existingIt != m_textures.end())
    {
        if (existingIt->second.descriptorSet != VK_NULL_HANDLE)
            return true;

        if (existingIt->second.texture)
        {
            existingIt->second.descriptorSet = ImGui_ImplVulkan_AddTexture(
                existingIt->second.texture->vkSampler(),
                existingIt->second.texture->vkImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            return existingIt->second.descriptorSet != VK_NULL_HANDLE;
        }

        m_textures.erase(existingIt);
    }

    engine::Texture::SharedPtr texture = nullptr;
    const std::string bundledSourcePath = resolveBundledEditorTextureSourcePath(key);
    if (!bundledSourcePath.empty())
        texture = loadTextureDirectlyFromImageSource(bundledSourcePath, VK_FORMAT_R8G8B8A8_SRGB);

    if (!texture && !assetPath.empty() && std::filesystem::path(assetPath).extension() != ".elixasset")
        texture = loadTextureDirectlyFromImageSource(assetPath, VK_FORMAT_R8G8B8A8_SRGB);

    if (!texture)
        texture = engine::AssetsLoader::loadTextureGPU(assetPath, VK_FORMAT_R8G8B8A8_SRGB);

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
        // Store texture with null descriptor so retry is possible on next getTextureDescriptorSet call
        m_textures[key] = TextureResource{.texture = std::move(texture), .descriptorSet = VK_NULL_HANDLE};
        return false;
    }

    m_textures[key] = TextureResource{
        .texture = std::move(texture),
        .descriptorSet = descriptorSet};

    return true;
}

ELIX_NESTED_NAMESPACE_END
