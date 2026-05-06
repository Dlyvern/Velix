#include "Core/Vulkan/VulkanContextBuilder.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

VulkanContext::Builder::Builder(platform::Window &window)
    : m_window(&window)
{
}

VulkanContext::Builder &VulkanContext::Builder::useEngineDefaults()
{
    request(VulkanFeature::Swapchain, FeatureRequirement::Required);
    request(VulkanFeature::DynamicRendering, FeatureRequirement::Required);
    request(VulkanFeature::Synchronization2, FeatureRequirement::Required);
    request(VulkanFeature::SamplerAnisotropy, FeatureRequirement::Required);
    request(VulkanFeature::ImageCubeArray, FeatureRequirement::Required);
    request(VulkanFeature::BufferDeviceAddress, FeatureRequirement::Preferred);
    request(VulkanFeature::TimelineSemaphore, FeatureRequirement::Preferred);
    request(VulkanFeature::DepthClamp, FeatureRequirement::Preferred);
    request(VulkanFeature::DescriptorIndexing, FeatureRequirement::Preferred);
    return *this;
}

VulkanContext::Builder &VulkanContext::Builder::applicationName(const char *name)
{
    m_applicationName = name ? name : "";
    return *this;
}

VulkanContext::Builder &VulkanContext::Builder::applicationVersion(uint32_t major, uint32_t minor, uint32_t patch)
{
    m_applicationVersion = VK_MAKE_VERSION(major, minor, patch);
    return *this;
}

VulkanContext::Builder &VulkanContext::Builder::engineName(const char *name)
{
    m_engineName = name ? name : "";
    return *this;
}

VulkanContext::Builder &VulkanContext::Builder::apiVersion(uint32_t version)
{
    m_apiVersion = version;
    return *this;
}

VulkanContext::Builder &VulkanContext::Builder::enableValidationLayers(bool enabled)
{
    m_validationLayersEnabled = enabled;
    return *this;
}

VulkanContext::Builder &VulkanContext::Builder::addInstanceExtension(const char *extensionName)
{
    if (extensionName)
        m_extraInstanceExtensions.push_back(extensionName);
    return *this;
}

VulkanContext::Builder &VulkanContext::Builder::addValidationLayer(const char *layerName)
{
    if (layerName)
        m_extraValidationLayers.push_back(layerName);
    return *this;
}

VulkanContext::Builder &VulkanContext::Builder::request(VulkanFeature feature, FeatureRequirement requirement)
{
    m_featureRequests[feature] = requirement;
    return *this;
}

VulkanContext::Builder &VulkanContext::Builder::preferredDeviceType(VkPhysicalDeviceType type)
{
    m_preferredDeviceType = type;
    return *this;
}

VulkanContext::Builder &VulkanContext::Builder::maxRequestedSampleCount(VkSampleCountFlagBits sampleCount)
{
    m_maxRequestedSampleCount = sampleCount;
    return *this;
}

std::shared_ptr<VulkanContext> VulkanContext::Builder::build()
{
    if (!m_window)
        throw std::runtime_error("VulkanContext::Builder: window is null");

    if (s_vulkanContext)
        return s_vulkanContext;

    auto ctx = std::shared_ptr<VulkanContext>(new VulkanContext(BuilderKey{}, *m_window));

    ctx->m_applicationName = m_applicationName;
    ctx->m_engineName = m_engineName;
    ctx->m_applicationVersion = m_applicationVersion;
    ctx->m_engineVersion = m_engineVersion;
    ctx->m_apiVersion = m_apiVersion;
    ctx->m_extraInstanceExtensions = m_extraInstanceExtensions;
    ctx->m_featureRequests = m_featureRequests;
    ctx->m_preferredDeviceType = m_preferredDeviceType;
    ctx->m_maxRequestedSampleCount = m_maxRequestedSampleCount;

    if (m_validationLayersEnabled.has_value())
        ctx->m_isValidationLayersEnabled = *m_validationLayersEnabled;
    else
    {
#ifdef DEBUG_BUILD
        ctx->m_isValidationLayersEnabled = true;
#else
        ctx->m_isValidationLayersEnabled = false;
#endif
    }

    for (const char *layer : m_extraValidationLayers)
    {
        const bool already = std::any_of(ctx->m_validationLayers.begin(), ctx->m_validationLayers.end(),
                                         [layer](const char *e) { return std::strcmp(e, layer) == 0; });
        if (!already)
            ctx->m_validationLayers.push_back(layer);
    }

    ctx->initVulkan(*m_window);

    s_vulkanContext = ctx;
    return ctx;
}

ELIX_NESTED_NAMESPACE_END
