#include "Engine/Components/ReflectionProbeComponent.hpp"

#include "Core/VulkanContext.hpp"

#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void ReflectionProbeComponent::setHDRPath(const std::string &path, VkDescriptorPool descriptorPool)
{
    hdrPath = path;
    reload(descriptorPool);
}

void ReflectionProbeComponent::reload(VkDescriptorPool descriptorPool)
{
    m_skybox.reset();
    if (hdrPath.empty() || !std::filesystem::exists(hdrPath))
        return;

    auto candidate = std::make_unique<Skybox>(hdrPath, descriptorPool);
    if (candidate->hasTexture())
        m_skybox = std::move(candidate);
}

void ReflectionProbeComponent::setCapturedCubemap(std::shared_ptr<core::Image> image,
                                                   VkImageView                  cubeView,
                                                   std::shared_ptr<core::Sampler> sampler)
{
    // Destroy old captured view if any
    if (m_capturedImageView != VK_NULL_HANDLE)
    {
        auto device = core::VulkanContext::getContext()->getDevice();
        vkDestroyImageView(device, m_capturedImageView, nullptr);
        m_capturedImageView = VK_NULL_HANDLE;
    }

    m_capturedImage     = std::move(image);
    m_capturedImageView = cubeView;
    m_capturedSampler   = std::move(sampler);
}

bool ReflectionProbeComponent::hasCubemap() const
{
    return m_capturedImageView != VK_NULL_HANDLE || (m_skybox && m_skybox->hasTexture());
}

VkImageView ReflectionProbeComponent::getProbeEnvView() const
{
    if (m_capturedImageView != VK_NULL_HANDLE)
        return m_capturedImageView;
    if (m_skybox && m_skybox->hasTexture())
        return m_skybox->getEnvImageView();
    return VK_NULL_HANDLE;
}

VkSampler ReflectionProbeComponent::getProbeEnvSampler() const
{
    if (m_capturedSampler && m_capturedImageView != VK_NULL_HANDLE)
        return m_capturedSampler->vk();
    if (m_skybox && m_skybox->hasTexture())
        return m_skybox->getEnvSampler();
    return VK_NULL_HANDLE;
}

void ReflectionProbeComponent::onDetach()
{
    if (m_capturedImageView != VK_NULL_HANDLE)
    {
        auto device = core::VulkanContext::getContext()->getDevice();
        vkDestroyImageView(device, m_capturedImageView, nullptr);
        m_capturedImageView = VK_NULL_HANDLE;
    }
    m_capturedImage.reset();
    m_capturedSampler.reset();
    m_skybox.reset();
}

ELIX_NESTED_NAMESPACE_END
