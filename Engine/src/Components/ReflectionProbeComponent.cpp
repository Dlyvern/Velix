#include "Engine/Components/ReflectionProbeComponent.hpp"

#include "Core/VulkanContext.hpp"

#include <filesystem>
#include <mutex>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace
{
    struct DeferredCapturedCubemapRelease
    {
        std::shared_ptr<core::Image> image;
        std::shared_ptr<core::Sampler> sampler;
        VkImageView imageView{VK_NULL_HANDLE};
    };

    std::mutex g_deferredCapturedCubemapReleasesMutex;
    std::vector<DeferredCapturedCubemapRelease> g_deferredCapturedCubemapReleases;
}

void ReflectionProbeComponent::releaseCapturedCubemap()
{
    if (m_capturedImageView == VK_NULL_HANDLE &&
        !m_capturedImage &&
        !m_capturedSampler)
        return;

    DeferredCapturedCubemapRelease pendingRelease{};
    pendingRelease.image = std::move(m_capturedImage);
    pendingRelease.sampler = std::move(m_capturedSampler);
    pendingRelease.imageView = m_capturedImageView;
    m_capturedImageView = VK_NULL_HANDLE;

    std::scoped_lock lock(g_deferredCapturedCubemapReleasesMutex);
    g_deferredCapturedCubemapReleases.push_back(std::move(pendingRelease));
}

void ReflectionProbeComponent::flushDeferredCapturedCubemapReleases()
{
    std::vector<DeferredCapturedCubemapRelease> pendingReleases;
    {
        std::scoped_lock lock(g_deferredCapturedCubemapReleasesMutex);
        if (g_deferredCapturedCubemapReleases.empty())
            return;

        pendingReleases.swap(g_deferredCapturedCubemapReleases);
    }

    auto context = core::VulkanContext::getContext();
    auto device = context->getDevice();
    vkDeviceWaitIdle(device);

    for (auto &pendingRelease : pendingReleases)
    {
        if (pendingRelease.imageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, pendingRelease.imageView, nullptr);
            pendingRelease.imageView = VK_NULL_HANDLE;
        }
    }
}

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
    releaseCapturedCubemap();

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
    releaseCapturedCubemap();
    m_skybox.reset();
}

ELIX_NESTED_NAMESPACE_END
