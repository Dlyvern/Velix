#include "Editor/EditorResourcesStorage.hpp"
#include "Core/VulkanContext.hpp"
#include <backends/imgui_impl_vulkan.h>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

void EditorResourcesStorage::loadNeededResources()
{
    auto logoTexture = std::make_shared<engine::Texture>();
    auto folderTexture = std::make_shared<engine::Texture>();
    auto fileTexture = std::make_shared<engine::Texture>();

    logoTexture->load("./resources/textures/VelixFire.png", core::VulkanContext::getContext()->getTransferCommandPool());
    auto logoDescriptorSet = ImGui_ImplVulkan_AddTexture(logoTexture->vkSampler(), logoTexture->vkImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    folderTexture->load("./resources/textures/folder.png", core::VulkanContext::getContext()->getTransferCommandPool());
    auto folderDescriptorSet = ImGui_ImplVulkan_AddTexture(folderTexture->vkSampler(), folderTexture->vkImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    fileTexture->load("./resources/textures/file.png", core::VulkanContext::getContext()->getTransferCommandPool());
    auto fileDescriptorSet = ImGui_ImplVulkan_AddTexture(fileTexture->vkSampler(), fileTexture->vkImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_textures["./resources/textures/VelixFire.png"] = TextureResource{
        .texture = logoTexture,
        .descriptorSet = logoDescriptorSet};

    m_textures["./resources/textures/folder.png"] = TextureResource{
        .texture = folderTexture,
        .descriptorSet = folderDescriptorSet};

    m_textures["./resources/textures/file.png"] = TextureResource{
        .texture = fileTexture,
        .descriptorSet = fileDescriptorSet};
}

VkDescriptorSet EditorResourcesStorage::getTextureDescriptorSet(const std::string &filePath)
{
    return m_textures[filePath].descriptorSet;
}

ELIX_NESTED_NAMESPACE_END
