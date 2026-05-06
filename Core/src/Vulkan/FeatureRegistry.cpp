#include "Core/Vulkan/FeatureRegistry.hpp"

#include "Core/Logger.hpp"
#include "Core/VulkanAssert.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

ELIX_NESTED_NAMESPACE_BEGIN(core)

namespace
{
    struct FeatureMeta
    {
        const char *name;
        std::vector<const char *> deviceExtensions;
        std::vector<VulkanFeature> dependencies;
    };

    const FeatureMeta &metaOf(VulkanFeature feature)
    {
        static const std::unordered_map<VulkanFeature, FeatureMeta> kTable = {
            {VulkanFeature::Swapchain, {"Swapchain", {VK_KHR_SWAPCHAIN_EXTENSION_NAME}, {}}},
            {VulkanFeature::DynamicRendering, {"DynamicRendering", {}, {}}},
            {VulkanFeature::Synchronization2, {"Synchronization2", {}, {}}},
            {VulkanFeature::SamplerAnisotropy, {"SamplerAnisotropy", {}, {}}},
            {VulkanFeature::ImageCubeArray, {"ImageCubeArray", {}, {}}},
            {VulkanFeature::BufferDeviceAddress, {"BufferDeviceAddress", {}, {}}},
            {VulkanFeature::TimelineSemaphore, {"TimelineSemaphore", {}, {}}},
            {VulkanFeature::DepthClamp, {"DepthClamp", {}, {}}},
            {VulkanFeature::DescriptorIndexing, {"DescriptorIndexing", {}, {}}},
            {VulkanFeature::DeferredHostOperations,
             {"DeferredHostOperations", {VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME}, {}}},
            {VulkanFeature::AccelerationStructure,
             {"AccelerationStructure",
              {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME},
              {VulkanFeature::DeferredHostOperations, VulkanFeature::BufferDeviceAddress}}},
            {VulkanFeature::RayTracingPipeline,
             {"RayTracingPipeline",
              {VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME},
              {VulkanFeature::AccelerationStructure}}},
            {VulkanFeature::RayQuery,
             {"RayQuery",
              {VK_KHR_RAY_QUERY_EXTENSION_NAME},
              {VulkanFeature::AccelerationStructure}}},
        };
        return kTable.at(feature);
    }

    void appendUnique(std::vector<const char *> &out, const char *name)
    {
        if (!name)
            return;
        for (const char *existing : out)
            if (std::strcmp(existing, name) == 0)
                return;
        out.push_back(name);
    }
}

FeatureRegistry::FeatureRegistry(std::unordered_map<VulkanFeature, FeatureRequirement> requests)
    : m_requests(std::move(requests))
{
}

bool FeatureRegistry::isRequested(VulkanFeature feature) const
{
    auto it = m_requests.find(feature);
    return it != m_requests.end() && it->second != FeatureRequirement::Disabled;
}

FeatureRequirement FeatureRegistry::requirementOf(VulkanFeature feature) const
{
    auto it = m_requests.find(feature);
    return it != m_requests.end() ? it->second : FeatureRequirement::Disabled;
}

bool FeatureRegistry::isExtensionAvailable(const char *name) const
{
    for (const auto &ext : m_deviceExtensions)
        if (std::strcmp(ext.extensionName, name) == 0)
            return true;
    return false;
}

bool FeatureRegistry::dependenciesSupported(VulkanFeature feature) const
{
    for (VulkanFeature dep : metaOf(feature).dependencies)
    {
        auto it = m_supported.find(dep);
        if (it == m_supported.end() || !it->second)
            return false;
    }
    return true;
}

void FeatureRegistry::probe(VkPhysicalDevice device)
{
    uint32_t count = 0;
    VX_VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr));
    m_deviceExtensions.resize(count);
    VX_VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, m_deviceExtensions.data()));

    m_supportedFeatures2.pNext = &m_supportedV12;
    m_supportedV12.pNext = &m_supportedV13;
    m_supportedV13.pNext = &m_supportedAS;
    m_supportedAS.pNext = &m_supportedRTP;
    m_supportedRTP.pNext = &m_supportedRQ;
    m_supportedRQ.pNext = nullptr;

    vkGetPhysicalDeviceFeatures2(device, &m_supportedFeatures2);

    auto extsPresent = [this](VulkanFeature f)
    {
        for (const char *ext : metaOf(f).deviceExtensions)
            if (!isExtensionAvailable(ext))
                return false;
        return true;
    };

    m_supported[VulkanFeature::Swapchain] = extsPresent(VulkanFeature::Swapchain);
    m_supported[VulkanFeature::DynamicRendering] = m_supportedV13.dynamicRendering == VK_TRUE;
    m_supported[VulkanFeature::Synchronization2] = m_supportedV13.synchronization2 == VK_TRUE;
    m_supported[VulkanFeature::SamplerAnisotropy] = m_supportedFeatures2.features.samplerAnisotropy == VK_TRUE;
    m_supported[VulkanFeature::ImageCubeArray] = m_supportedFeatures2.features.imageCubeArray == VK_TRUE;
    m_supported[VulkanFeature::BufferDeviceAddress] = m_supportedV12.bufferDeviceAddress == VK_TRUE;
    m_supported[VulkanFeature::TimelineSemaphore] = m_supportedV12.timelineSemaphore == VK_TRUE;
    m_supported[VulkanFeature::DepthClamp] = m_supportedFeatures2.features.depthClamp == VK_TRUE;
    m_supported[VulkanFeature::DescriptorIndexing] =
        m_supportedV12.descriptorBindingPartiallyBound == VK_TRUE &&
        m_supportedV12.runtimeDescriptorArray == VK_TRUE;
    m_supported[VulkanFeature::DeferredHostOperations] = extsPresent(VulkanFeature::DeferredHostOperations);
    m_supported[VulkanFeature::AccelerationStructure] =
        extsPresent(VulkanFeature::AccelerationStructure) &&
        m_supportedAS.accelerationStructure == VK_TRUE;
    m_supported[VulkanFeature::RayTracingPipeline] =
        extsPresent(VulkanFeature::RayTracingPipeline) &&
        m_supportedRTP.rayTracingPipeline == VK_TRUE;
    m_supported[VulkanFeature::RayQuery] =
        extsPresent(VulkanFeature::RayQuery) &&
        m_supportedRQ.rayQuery == VK_TRUE;

    for (const auto &[feature, requirement] : m_requests)
    {
        if (requirement != FeatureRequirement::Required)
            continue;

        const bool deps = dependenciesSupported(feature);
        if (!m_supported[feature] || !deps)
            throw std::runtime_error(std::string("Required Vulkan feature unavailable: ") +
                                     metaOf(feature).name);
    }
}

void FeatureRegistry::disableAllOptional()
{
    for (auto &[feature, requirement] : m_requests)
        if (requirement == FeatureRequirement::Optional)
            requirement = FeatureRequirement::Disabled;
    m_enabled.clear();
}

bool FeatureRegistry::isSupported(VulkanFeature feature) const
{
    auto it = m_supported.find(feature);
    return it != m_supported.end() && it->second;
}

bool FeatureRegistry::isEnabled(VulkanFeature feature) const
{
    auto it = m_enabled.find(feature);
    return it != m_enabled.end() && it->second;
}

void FeatureRegistry::enableFeature(VulkanFeature feature)
{
    if (m_enabled[feature])
        return;

    m_enabled[feature] = true;

    for (VulkanFeature dep : metaOf(feature).dependencies)
        if (m_supported[dep])
            enableFeature(dep);
}

void FeatureRegistry::buildEnableChain(std::vector<const char *> &outDeviceExtensions,
                                       VkPhysicalDeviceFeatures2 *&outFeatures2)
{
    m_enabled.clear();

    m_enableFeatures2 = VkPhysicalDeviceFeatures2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    m_enableV12 = VkPhysicalDeviceVulkan12Features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    m_enableV13 = VkPhysicalDeviceVulkan13Features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    m_enableAS = VkPhysicalDeviceAccelerationStructureFeaturesKHR{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    m_enableRTP = VkPhysicalDeviceRayTracingPipelineFeaturesKHR{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    m_enableRQ = VkPhysicalDeviceRayQueryFeaturesKHR{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

    for (const auto &[feature, requirement] : m_requests)
    {
        if (requirement == FeatureRequirement::Disabled)
            continue;
        if (!m_supported[feature] || !dependenciesSupported(feature))
            continue;
        enableFeature(feature);
    }

    for (const auto &[feature, on] : m_enabled)
        if (on)
            for (const char *ext : metaOf(feature).deviceExtensions)
                appendUnique(outDeviceExtensions, ext);

    if (isEnabled(VulkanFeature::SamplerAnisotropy))
        m_enableFeatures2.features.samplerAnisotropy = VK_TRUE;
    if (isEnabled(VulkanFeature::ImageCubeArray))
        m_enableFeatures2.features.imageCubeArray = VK_TRUE;
    if (isEnabled(VulkanFeature::DepthClamp))
        m_enableFeatures2.features.depthClamp = VK_TRUE;

    m_enableFeatures2.features.shaderInt64 = m_supportedFeatures2.features.shaderInt64;
    m_enableFeatures2.features.fillModeNonSolid = m_supportedFeatures2.features.fillModeNonSolid;
    m_enableFeatures2.features.independentBlend = m_supportedFeatures2.features.independentBlend;

    if (isEnabled(VulkanFeature::BufferDeviceAddress))
        m_enableV12.bufferDeviceAddress = VK_TRUE;
    if (isEnabled(VulkanFeature::TimelineSemaphore))
        m_enableV12.timelineSemaphore = VK_TRUE;
    if (isEnabled(VulkanFeature::DescriptorIndexing))
    {
        m_enableV12.descriptorBindingPartiallyBound = VK_TRUE;
        m_enableV12.runtimeDescriptorArray = VK_TRUE;
        m_enableV12.shaderSampledImageArrayNonUniformIndexing =
            m_supportedV12.shaderSampledImageArrayNonUniformIndexing;
    }

    if (isEnabled(VulkanFeature::DynamicRendering))
        m_enableV13.dynamicRendering = VK_TRUE;
    if (isEnabled(VulkanFeature::Synchronization2))
        m_enableV13.synchronization2 = VK_TRUE;

    void **lastNext = nullptr;
    m_enableFeatures2.pNext = &m_enableV12;
    m_enableV12.pNext = &m_enableV13;
    m_enableV13.pNext = nullptr;
    lastNext = &m_enableV13.pNext;

    if (isEnabled(VulkanFeature::AccelerationStructure))
    {
        m_enableAS.accelerationStructure = VK_TRUE;
        m_enableAS.pNext = nullptr;
        *lastNext = &m_enableAS;
        lastNext = &m_enableAS.pNext;
    }
    if (isEnabled(VulkanFeature::RayTracingPipeline))
    {
        m_enableRTP.rayTracingPipeline = VK_TRUE;
        m_enableRTP.pNext = nullptr;
        *lastNext = &m_enableRTP;
        lastNext = &m_enableRTP.pNext;
    }
    if (isEnabled(VulkanFeature::RayQuery))
    {
        m_enableRQ.rayQuery = VK_TRUE;
        m_enableRQ.pNext = nullptr;
        *lastNext = &m_enableRQ;
        lastNext = &m_enableRQ.pNext;
    }

    outFeatures2 = &m_enableFeatures2;
}

ELIX_NESTED_NAMESPACE_END
