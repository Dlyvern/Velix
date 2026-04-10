#include "Core/VulkanContext.hpp"
#include "Core/VulkanAssert.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <set>
#include <limits>
#include <algorithm>
#include <unordered_map>
#include <array>

#include "Core/Memory/VMAAllocator.hpp"
#include "Core/VulkanHelpers.hpp"

#include "Core/Logger.hpp"

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                    VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        VX_CORE_ERROR_STREAM((std::string(pCallbackData->pMessage)) << std::endl);
    // else if(messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
    //     VX_CORE_INFO_STREAM((std::string(pCallbackData->pMessage)) << std::endl);
    else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        VX_CORE_INFO_STREAM((std::string(pCallbackData->pMessage)) << std::endl);
    else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        VX_CORE_ERROR_STREAM((std::string(pCallbackData->pMessage)) << std::endl);

    return VK_FALSE;
}

ELIX_NESTED_NAMESPACE_BEGIN(core)

namespace
{
    VkSampleCountFlagBits highestSupportedSampleCount(VkSampleCountFlags counts)
    {
        constexpr std::array<VkSampleCountFlagBits, 7> kDescendingSampleCounts{
            VK_SAMPLE_COUNT_64_BIT,
            VK_SAMPLE_COUNT_32_BIT,
            VK_SAMPLE_COUNT_16_BIT,
            VK_SAMPLE_COUNT_8_BIT,
            VK_SAMPLE_COUNT_4_BIT,
            VK_SAMPLE_COUNT_2_BIT,
            VK_SAMPLE_COUNT_1_BIT,
        };

        for (const auto sampleCount : kDescendingSampleCounts)
        {
            if ((counts & sampleCount) != 0u)
                return sampleCount;
        }

        return VK_SAMPLE_COUNT_1_BIT;
    }
}

std::shared_ptr<VulkanContext> VulkanContext::create(platform::Window &window)
{
    if (s_vulkanContext)
        return s_vulkanContext;

    s_vulkanContext = std::make_shared<VulkanContext>(window);

    return s_vulkanContext;
}

std::shared_ptr<VulkanContext> VulkanContext::getContext()
{
    if (!s_vulkanContext)
        throw std::runtime_error("NO VULKAN CONTEXT CREATED");

    return s_vulkanContext;
}

VulkanContext::VulkanContext(platform::Window &window)
{
#ifdef DEBUG_BUILD
    m_isValidationLayersEnabled = true;
#else
    // Allow validation layers in Release when explicitly requested.
    m_isValidationLayersEnabled = false;
#endif

    initVulkan(window);
}

VulkanContext::~VulkanContext()
{
    if (!m_isCleanedUp)
    {
        VX_CORE_ERROR_STREAM("ERROR: VulkanContext destroyed without calling cleanup()!!!" << std::endl);
        // DO NOT CALL cleanup here. It is too late...
    }
}

void VulkanContext::initVulkan(platform::Window &window)
{
    m_window = &window;
    VX_CORE_INFO_STREAM("[Startup] Initializing Vulkan");
    VX_VK_CHECK(volkInitialize());

    VX_CORE_INFO_STREAM("[Startup] Creating Vulkan instance");
    createInstance();
    VX_CORE_INFO_STREAM("[Startup] Creating Vulkan surface");
    createDebugger();
    createSurface(window);
    VX_CORE_INFO_STREAM("[Startup] Selecting Vulkan physical device");
    pickPhysicalDevice();
    VX_CORE_INFO_STREAM("[Startup] Creating Vulkan logical device");
    createLogicalDevice();

    VX_CORE_INFO_STREAM("[Startup] Creating Vulkan swapchain");
    m_swapChain = SwapChain::create(window, m_surface, m_device, m_physicalDevice, m_queueFamilyIndices.graphicsFamily.value(), m_queueFamilyIndices.presentFamily.value());
    VX_CORE_INFO_STREAM("[Startup] Vulkan initialized");
}

void VulkanContext::createLogicalDevice()
{
    VkPhysicalDeviceAccelerationStructureFeaturesKHR supportedAS{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR supportedRTP{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR supportedRQ{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceVulkan12Features supportedV12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features supportedV13{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 supportedFeatures2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

    supportedFeatures2.pNext = &supportedV12;
    supportedV12.pNext = &supportedV13;
    supportedV13.pNext = &supportedAS;
    supportedAS.pNext = &supportedRTP;
    supportedRTP.pNext = &supportedRQ;
    supportedRQ.pNext = nullptr;

    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &supportedFeatures2);

    if (!supportedFeatures2.features.samplerAnisotropy)
        throw std::runtime_error("Selected GPU does not support samplerAnisotropy");

    if (!supportedFeatures2.features.imageCubeArray)
        throw std::runtime_error("Selected GPU does not support imageCubeArray");

    m_bufferDeviceAddressSupported = supportedV12.bufferDeviceAddress == VK_TRUE;
    m_depthClampSupported = supportedFeatures2.features.depthClamp == VK_TRUE;
    m_timelineSemaphoreSupported = supportedV12.timelineSemaphore == VK_TRUE;

    if (!supportedV13.dynamicRendering)
        throw std::runtime_error("Selected GPU does not support dynamicRendering");

    if (!supportedV13.synchronization2)
        throw std::runtime_error("Selected GPU does not support synchronization2");

    m_queueFamilyIndices = findQueueFamilies(m_physicalDevice, m_surface);

    VX_CORE_INFO_STREAM("[Vulkan] Queue families found:");
    VX_CORE_INFO_STREAM("  graphics: " << m_queueFamilyIndices.graphicsFamily.value());
    VX_CORE_INFO_STREAM("  present:  " << m_queueFamilyIndices.presentFamily.value());
    VX_CORE_INFO_STREAM("  compute:  " << m_queueFamilyIndices.computeFamily.value());
    if (m_queueFamilyIndices.computeFamily == m_queueFamilyIndices.graphicsFamily)
        VX_CORE_INFO_STREAM(" (fallback to graphics)");
    VX_CORE_INFO_STREAM("  transfer: " << m_queueFamilyIndices.transferFamily.value());
    if (m_queueFamilyIndices.transferFamily == m_queueFamilyIndices.graphicsFamily)
        VX_CORE_INFO_STREAM(" (fallback to graphics)");

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        m_queueFamilyIndices.graphicsFamily.value(),
        m_queueFamilyIndices.presentFamily.value(),
        m_queueFamilyIndices.computeFamily.value(),
        m_queueFamilyIndices.transferFamily.value()};

    float queuePriority = 1.0f;

    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    std::vector<const char *> enabledExtensions = m_deviceExtensions;
    auto addExtensionIfMissing = [&enabledExtensions](const char *extensionName)
    {
        if (!extensionName)
            return;

        if (std::find_if(enabledExtensions.begin(), enabledExtensions.end(), [extensionName](const char *existing)
                         { return std::strcmp(existing, extensionName) == 0; }) == enabledExtensions.end())
            enabledExtensions.push_back(extensionName);
    };

    auto availableExtensions = enumerateDeviceExtensions(m_physicalDevice);

    const bool hasAS = hasExtension(availableExtensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    const bool hasRTP = hasExtension(availableExtensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    const bool hasRQ = hasExtension(availableExtensions, VK_KHR_RAY_QUERY_EXTENSION_NAME);
    const bool hasDHO = hasExtension(availableExtensions, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

    m_rayTracingSupport = {};

    if (m_bufferDeviceAddressSupported &&
        hasAS && hasRTP && hasDHO &&
        supportedAS.accelerationStructure &&
        supportedRTP.rayTracingPipeline)
    {
        m_rayTracingSupport.rayTracingPipeline = true;
        m_rayTracingMode = RayTracingMode::Pipeline;

        addExtensionIfMissing(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        addExtensionIfMissing(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        addExtensionIfMissing(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

        // Also enable ray query if available — allows inline ray tracing in fragment shaders
        // alongside the ray tracing pipeline (used for RT reflections).
        if (hasRQ && supportedRQ.rayQuery)
        {
            m_rayTracingSupport.rayQuery = true;
            addExtensionIfMissing(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            VX_CORE_INFO_STREAM("[Vulkan] Ray tracing mode: Pipeline + RayQuery");
        }
        else
            VX_CORE_INFO_STREAM("[Vulkan] Ray tracing mode: Pipeline");
    }
    else if (m_bufferDeviceAddressSupported &&
             hasAS && hasRQ && hasDHO &&
             supportedAS.accelerationStructure &&
             supportedRQ.rayQuery)
    {
        m_rayTracingSupport.rayQuery = true;
        m_rayTracingMode = RayTracingMode::RayQuery;

        addExtensionIfMissing(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        addExtensionIfMissing(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        addExtensionIfMissing(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

        VX_CORE_INFO_STREAM("[Vulkan] Ray tracing mode: RayQuery");
    }
    else
        VX_CORE_INFO_STREAM("[Vulkan] Ray tracing mode: None");

    m_rayTracingPipelineProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    m_accelerationStructureProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};

    VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    VkPhysicalDeviceDepthStencilResolveProperties depthStencilResolveProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES};
    properties2.pNext = &depthStencilResolveProperties;
    m_rayTracingPipelineProperties.pNext = nullptr;
    m_accelerationStructureProperties.pNext = nullptr;

    if (hasRTP)
    {
        depthStencilResolveProperties.pNext = &m_rayTracingPipelineProperties;
        if (hasAS)
            m_rayTracingPipelineProperties.pNext = &m_accelerationStructureProperties;
    }
    else if (hasAS)
    {
        depthStencilResolveProperties.pNext = &m_accelerationStructureProperties;
    }
    else
        depthStencilResolveProperties.pNext = nullptr;

    vkGetPhysicalDeviceProperties2(m_physicalDevice, &properties2);

    const VkPhysicalDeviceProperties &physicalDeviceProperties = properties2.properties;
    const VkSampleCountFlags commonSampleCounts =
        physicalDeviceProperties.limits.framebufferColorSampleCounts &
        physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    m_maxUsableSampleCount = highestSupportedSampleCount(commonSampleCounts);
    m_sampleZeroDepthResolveSupported =
        depthStencilResolveProperties.supportedDepthResolveModes != VK_RESOLVE_MODE_NONE &&
        (depthStencilResolveProperties.supportedDepthResolveModes & VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) != 0u;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtpFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR rqFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.features.samplerAnisotropy = VK_TRUE;
    features2.features.imageCubeArray = VK_TRUE;
    features2.features.depthClamp = m_depthClampSupported ? VK_TRUE : VK_FALSE;
    features2.features.shaderInt64 = supportedFeatures2.features.shaderInt64;

    features2.features.fillModeNonSolid = supportedFeatures2.features.fillModeNonSolid;
    features2.features.independentBlend = supportedFeatures2.features.independentBlend;

    VkPhysicalDeviceVulkan12Features v12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    v12.bufferDeviceAddress = m_bufferDeviceAddressSupported ? VK_TRUE : VK_FALSE;
    v12.timelineSemaphore = m_timelineSemaphoreSupported ? VK_TRUE : VK_FALSE;
    v12.descriptorBindingPartiallyBound = supportedV12.descriptorBindingPartiallyBound;
    v12.runtimeDescriptorArray = supportedV12.runtimeDescriptorArray;
    v12.shaderSampledImageArrayNonUniformIndexing = supportedV12.shaderSampledImageArrayNonUniformIndexing;

    VkPhysicalDeviceVulkan13Features v13{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    v13.dynamicRendering = VK_TRUE;
    v13.synchronization2 = VK_TRUE;

    features2.pNext = &v12;
    v12.pNext = &v13;
    v13.pNext = nullptr;

    if (m_rayTracingMode == RayTracingMode::Pipeline)
    {
        asFeatures.accelerationStructure = VK_TRUE;
        rtpFeatures.rayTracingPipeline = VK_TRUE;

        v13.pNext = &asFeatures;
        asFeatures.pNext = &rtpFeatures;

        if (m_rayTracingSupport.rayQuery)
        {
            rqFeatures.rayQuery = VK_TRUE;
            rtpFeatures.pNext = &rqFeatures;
            rqFeatures.pNext = nullptr;
        }
        else
            rtpFeatures.pNext = nullptr;
    }
    else if (m_rayTracingMode == RayTracingMode::RayQuery)
    {
        asFeatures.accelerationStructure = VK_TRUE;
        rqFeatures.rayQuery = VK_TRUE;

        v13.pNext = &asFeatures;
        asFeatures.pNext = &rqFeatures;
        rqFeatures.pNext = nullptr;
    }

    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = nullptr;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = &features2;

    if (m_isValidationLayersEnabled)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }

    VkResult createDeviceResult = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_vkDevice);
    if (createDeviceResult != VK_SUCCESS && m_rayTracingMode != static_cast<RayTracingMode>(0))
    {
        VX_CORE_WARNING_STREAM("[Vulkan] vkCreateDevice failed with ray tracing enabled ("
                               << core::helpers::vulkanResultToString(createDeviceResult)
                               << "). Retrying without ray tracing extensions/features.\n");

        m_rayTracingMode = static_cast<RayTracingMode>(0);
        m_rayTracingSupport = {};

        enabledExtensions = m_deviceExtensions;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
        createInfo.ppEnabledExtensionNames = enabledExtensions.data();

        v13.pNext = nullptr;
        asFeatures.pNext = nullptr;
        rtpFeatures.pNext = nullptr;
        rqFeatures.pNext = nullptr;
        asFeatures.accelerationStructure = VK_FALSE;
        rtpFeatures.rayTracingPipeline = VK_FALSE;
        rqFeatures.rayQuery = VK_FALSE;

        createDeviceResult = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_vkDevice);
    }

    if (createDeviceResult != VK_SUCCESS)
        throw std::runtime_error("Failed to create logical device: " + core::helpers::vulkanResultToString(createDeviceResult));

    vkGetDeviceQueue(m_vkDevice, m_queueFamilyIndices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_vkDevice, m_queueFamilyIndices.presentFamily.value(), 0, &m_presentQueue);
    vkGetDeviceQueue(m_vkDevice, m_queueFamilyIndices.transferFamily.value(), 0, &m_transferQueue);
    vkGetDeviceQueue(m_vkDevice, m_queueFamilyIndices.computeFamily.value(), 0, &m_computeQueue);

    volkLoadDevice(m_vkDevice);

    m_transferCommandPool = CommandPool::createShared(
        m_vkDevice,
        m_queueFamilyIndices.transferFamily.value(),
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    m_graphicsCommandPool = CommandPool::createShared(
        m_vkDevice,
        m_queueFamilyIndices.graphicsFamily.value(),
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    std::vector<VkDescriptorPoolSize> descriptorPoolSizes = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 2000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2000},
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 256},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    };

    m_descriptorPool = DescriptorPool::createShared(
        m_vkDevice,
        descriptorPoolSizes,
        VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        8000);

    auto vmaAllocator = std::make_unique<allocators::VMAAllocator>(
        m_instance,
        m_physicalDevice,
        m_vkDevice,
        VK_API_VERSION_1_3,
        m_bufferDeviceAddressSupported);

    m_device = Device::createShared(
        m_vkDevice,
        m_physicalDevice,
        std::move(vmaAllocator));
}

DescriptorPool::SharedPtr VulkanContext::getPersistentDescriptorPool() const
{
    return m_descriptorPool;
}

bool VulkanContext::hasBufferDeviceAddressSupport() const
{
    return m_bufferDeviceAddressSupported;
}

bool VulkanContext::hasAccelerationStructureSupport() const
{
    return m_rayTracingMode != RayTracingMode::Disabled;
}

bool VulkanContext::hasDepthClampSupport() const
{
    return m_depthClampSupported;
}

bool VulkanContext::hasRayQuerySupport() const
{
    return m_rayTracingSupport.rayQuery;
}

bool VulkanContext::hasRayTracingPipelineSupport() const
{
    return m_rayTracingSupport.rayTracingPipeline;
}

bool VulkanContext::hasTimelineSemaphoreSupport() const
{
    return m_timelineSemaphoreSupported;
}

VkSampleCountFlagBits VulkanContext::getMaxUsableSampleCount() const
{
    return m_maxUsableSampleCount;
}

VkSampleCountFlagBits VulkanContext::clampSupportedSampleCount(VkSampleCountFlagBits requested) const
{
    if (requested <= VK_SAMPLE_COUNT_1_BIT)
        return VK_SAMPLE_COUNT_1_BIT;

    return (std::min)(requested, m_maxUsableSampleCount);
}

VkSampleCountFlagBits VulkanContext::getEffectiveMsaaSampleCount(VkSampleCountFlagBits requested) const
{
    const VkSampleCountFlagBits clampedSampleCount = clampSupportedSampleCount(requested);
    if (clampedSampleCount <= VK_SAMPLE_COUNT_1_BIT)
        return VK_SAMPLE_COUNT_1_BIT;

    if (!m_sampleZeroDepthResolveSupported)
        return VK_SAMPLE_COUNT_1_BIT;

    return clampedSampleCount;
}

bool VulkanContext::supportsSampleZeroDepthResolve() const
{
    return m_sampleZeroDepthResolveSupported;
}

bool VulkanContext::hasRayTracingDeviceFeaturesEnabled() const
{
    return m_rayTracingMode != RayTracingMode::Disabled;
}

const VkPhysicalDeviceRayTracingPipelinePropertiesKHR &VulkanContext::getRayTracingPipelineProperties() const
{
    return m_rayTracingPipelineProperties;
}

uint32_t VulkanContext::getGraphicsFamily() const
{
    return m_queueFamilyIndices.graphicsFamily.value();
}

uint32_t VulkanContext::getTransferFamily() const
{
    return m_queueFamilyIndices.transferFamily.value();
}

uint32_t VulkanContext::getComputeFamily() const
{
    return m_queueFamilyIndices.computeFamily.value();
}

const VulkanContext::QueueFamilyIndices &VulkanContext::getQueueFamilyIndices() const
{
    return m_queueFamilyIndices;
}

VulkanContext::QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    uint32_t queueFamilyCount{0};

    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamiles(queueFamilyCount);

    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamiles.data());

    QueueFamilyIndices indices;
    std::optional<uint32_t> firstComputeFamily;
    std::optional<uint32_t> dedicatedComputeFamily;
    std::optional<uint32_t> firstTransferFamily;
    std::optional<uint32_t> asyncTransferFamily;
    std::optional<uint32_t> dedicatedTransferFamily;

    for (int index = 0; index < queueFamilyCount; ++index)
    {
        VkBool32 presentSupport{VK_FALSE};

        const auto &queueFamily = queueFamiles[index];

        VX_VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &presentSupport));

        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            if (!indices.graphicsFamily.has_value())
                indices.graphicsFamily = index;

            if (presentSupport)
                indices.presentFamily = index;
        }

        if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            if (!firstComputeFamily.has_value())
                firstComputeFamily = index;

            if (!(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT))
                dedicatedComputeFamily = index;
        }

        if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT)
        {
            if (!firstTransferFamily.has_value())
                firstTransferFamily = index;

            const bool hasGraphics = (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            const bool hasCompute = (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;

            if (!hasGraphics && !hasCompute)
                dedicatedTransferFamily = index;
            else if (!hasGraphics && !asyncTransferFamily.has_value())
                asyncTransferFamily = index;
        }

        if (presentSupport && !indices.presentFamily.has_value())
            indices.presentFamily = index;
    }

    if (dedicatedComputeFamily.has_value())
        indices.computeFamily = dedicatedComputeFamily;
    else if (firstComputeFamily.has_value())
        indices.computeFamily = firstComputeFamily;

    if (dedicatedTransferFamily.has_value())
        indices.transferFamily = dedicatedTransferFamily;
    else if (asyncTransferFamily.has_value())
        indices.transferFamily = asyncTransferFamily;
    else if (firstTransferFamily.has_value())
        indices.transferFamily = firstTransferFamily;

    if (!indices.hasCompute())
        indices.computeFamily = indices.graphicsFamily;

    if (!indices.hasTransfer())
        indices.transferFamily = indices.graphicsFamily;

    return indices;
}

VkPhysicalDeviceFeatures VulkanContext::getPhysicalDeviceFeatures()
{
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &features);
    return features;
}

std::vector<VkExtensionProperties> VulkanContext::enumerateDeviceExtensions(VkPhysicalDevice physicalDevice)
{
    uint32_t extensionCount{0};

    VX_VK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr));

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);

    VX_VK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data()));

    return availableExtensions;
}

bool VulkanContext::hasExtension(const std::vector<VkExtensionProperties> &extensions, const char *name)
{
    for (const auto &ext : extensions)
        if (strcmp(ext.extensionName, name) == 0)
            return true;

    return false;
}

bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice device)
{
    auto extensions = enumerateDeviceExtensions(device);

    for (const auto &ext : m_deviceExtensions)
        if (!hasExtension(extensions, ext))
            return false;

    return true;
}

VulkanContext::RayTracingSupport VulkanContext::hasRTXSupport(VkPhysicalDevice physicalDevice)
{
    auto extensions = enumerateDeviceExtensions(physicalDevice);
    RayTracingSupport out{};

    const bool hasAS = hasExtension(extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    const bool hasRTP = hasExtension(extensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    const bool hasRQ = hasExtension(extensions, VK_KHR_RAY_QUERY_EXTENSION_NAME);
    const bool hasDHO = hasExtension(extensions, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtpFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};

    VkPhysicalDeviceRayQueryFeaturesKHR rqFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &asFeatures;
    asFeatures.pNext = &rtpFeatures;
    rtpFeatures.pNext = &rqFeatures;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

    out.rayTracingPipeline =
        hasAS &&
        hasRTP &&
        hasDHO &&
        asFeatures.accelerationStructure &&
        rtpFeatures.rayTracingPipeline;

    out.rayQuery =
        hasAS &&
        hasRQ &&
        hasDHO &&
        asFeatures.accelerationStructure &&
        rqFeatures.rayQuery;

    return out;
}

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device)
{
    QueueFamilyIndices indices = findQueueFamilies(device, m_surface);

    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate{false};

    if (extensionsSupported)
    {
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;

        uint32_t formatCount{0};

        VX_VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr));

        if (formatCount != 0)
        {
            formats.resize(formatCount);
            VX_VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, formats.data()));
        }

        uint32_t presentModeCount{0};

        VX_VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr));

        if (presentModeCount != 0)
        {
            presentModes.resize(presentModeCount);
            VX_VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, presentModes.data()));
        }

        swapChainAdequate = !formats.empty() && !presentModes.empty();
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(device, &features);

    return indices.isComplete() && extensionsSupported && swapChainAdequate && features.samplerAnisotropy && features.imageCubeArray && properties.apiVersion >= VK_API_VERSION_1_3;
}

void VulkanContext::pickPhysicalDevice()
{
    uint32_t deviceCount{0};

    VX_VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr));

    if (deviceCount == 0)
        throw std::runtime_error("Failed to find GPUs with Vulkan support");

    std::vector<VkPhysicalDevice> devices(deviceCount);

    VX_VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data()));

    struct Candidate
    {
        VkPhysicalDevice device;
        VkPhysicalDeviceProperties props;
        int score;
        bool canPresent;
    };

    std::vector<Candidate> candidates;

    for (const auto &device : devices)
    {
        if (!isDeviceSuitable(device))
            continue;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        bool canPresent = false;

        for (uint32_t i = 0; i < queueFamilyCount; ++i)
        {
            VkBool32 support = VK_FALSE;
            VX_VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &support));

            if (support)
            {
                canPresent = true;
                break;
            }
        }

        if (!canPresent)
        {
            VX_CORE_INFO_STREAM("[Vulkan] Skipping GPU " << props.deviceName << " because it cannot present to this surface.\n");
            continue;
        }

        int score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            score += 1000;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
            score += 500;
        else
            score += 100;

        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(device, &memProps);
        for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
            if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                score += static_cast<int>(memProps.memoryHeaps[i].size / (1024 * 1024 * 1024));

        candidates.push_back({device, props, score, canPresent});
    }

    if (candidates.empty())
        throw std::runtime_error("Failed to find a suitable GPU");

    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b)
              { return a.score > b.score; });

    for (const auto &candidate : candidates)
    {
        VX_CORE_INFO_STREAM("GPU: " << candidate.props.deviceName);
        VX_CORE_INFO_STREAM("Score: " << candidate.score);
    }

    m_physicalDevice = candidates.front().device;
    if (!m_physicalDevice)
        throw std::runtime_error("Failed to find a suitable GPU");

    const auto &selected = candidates.front().props;
    auto toDeviceTypeName = [](VkPhysicalDeviceType type) -> const char *
    {
        switch (type)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return "Discrete";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return "Integrated";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return "Virtual";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return "CPU";
        default:
            return "Other";
        }
    };

    VX_CORE_INFO_STREAM("[Vulkan] Selected GPU: " << selected.deviceName
                                                  << " ("
                                                  << toDeviceTypeName(selected.deviceType)
                                                  << ", vendor 0x" << std::hex << selected.vendorID
                                                  << ", device 0x" << selected.deviceID
                                                  << std::dec << ")\n");

    const bool selectedHardwareGpu = selected.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
                                     selected.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    if (!selectedHardwareGpu)
    {
        VX_CORE_WARNING_STREAM("[Vulkan] Selected adapter is not a hardware GPU (" << toDeviceTypeName(selected.deviceType)
                                                                                   << "). Performance may be very poor. "
                                                                                   << "Please update/reinstall graphics drivers and ensure Vulkan runs on your real GPU.\n");
    }
}

void VulkanContext::createSurface(platform::Window &window)
{
    VX_VK_CHECK(glfwCreateWindowSurface(m_instance, window.getRawHandler(), nullptr, &m_surface));
}

void VulkanContext::createInstance()
{
    VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    applicationInfo.pApplicationName = "VelixCore";
    applicationInfo.pEngineName = "VelixEngine";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    applicationInfo.engineVersion = VK_MAKE_VERSION(0, 0, 1);
    applicationInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t glfwExtensionCount{0};

    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    uint32_t extensionCount{0};

    VX_VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr));

    std::vector<VkExtensionProperties> extensions(extensionCount);

    VX_VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data()));

    std::unordered_map<std::string, bool> presentedExtensions;

    for (int index = 0; index < glfwExtensionCount; ++index)
    {
        std::string glfwExtension{glfwExtensions[index]};
        presentedExtensions[glfwExtension] = false;
#ifdef DEBUG_BUILD
        VX_CORE_INFO_STREAM(("GLFW extension: ") << glfwExtensions[index]);
#endif
    }

    for (const auto &extension : extensions)
    {
#ifdef DEBUG_BUILD
        VX_CORE_INFO_STREAM(("Vulkan extension: ") << extension.extensionName);
#endif
        auto it = presentedExtensions.find(extension.extensionName);

        if (it != presentedExtensions.end())
            it->second = true;
    }

    for (const auto &[name, found] : presentedExtensions)
        if (!found)
            throw std::runtime_error("Failed to find GLFW extension in Vulkan " + name);

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;

    bool validationLayersEnabledForInstance = false;

    if (m_isValidationLayersEnabled)
    {
        if (!checkValidationLayers())
        {
            VX_CORE_ERROR_STREAM(("Validation layers required, but not available") << std::endl);
            m_isValidationLayersEnabled = false;
        }
        else
        {
            createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
            createInfo.ppEnabledLayerNames = m_validationLayers.data();
            validationLayersEnabledForInstance = true;
        }
    }
    else
        createInfo.enabledLayerCount = 0;

    auto newExtensions = getRequiredExtensions();

    createInfo.enabledExtensionCount = newExtensions.size();
    createInfo.ppEnabledExtensionNames = newExtensions.data();

    VX_VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance));

    if (validationLayersEnabledForInstance)
        VX_CORE_INFO_STREAM("[Vulkan] Validation layers enabled");

    volkLoadInstance(m_instance);
}

bool VulkanContext::checkValidationLayers()
{
    uint32_t layerCount;

    VX_VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, nullptr));

    std::vector<VkLayerProperties> availableLayers(layerCount);

    VX_VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data()));

    bool layerFound{false};

    for (const auto &layerName : m_validationLayers)
    {
        for (const auto &availableLayer : availableLayers)
        {
            if (strcmp(layerName, availableLayer.layerName) == 0)
            {
                layerFound = true;
                break;
            }
        }
    }

    return layerFound;
}

VkPhysicalDeviceProperties VulkanContext::getPhysicalDevicePoperties()
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    return properties;
}

std::vector<const char *> VulkanContext::getRequiredExtensions()
{
    uint32_t glfwExtensionCount{0};

    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char *> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (m_isValidationLayersEnabled)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    return extensions;
}

void VulkanContext::createDebugger()
{
    if (!m_isValidationLayersEnabled)
        return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.flags = 0;

    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

    createInfo.pfnUserCallback = debugCallback;
    createInfo.pUserData = nullptr;

    auto function = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");

    if (function)
    {
        if (VX_VK_TRY(function(m_instance, &createInfo, nullptr, &m_debugMessenger)) == VK_SUCCESS)
            VX_CORE_INFO_STREAM("[Vulkan] Debug utils messenger created");
    }
    else
    {
        VX_CORE_WARNING_STREAM("[Vulkan] Validation layers requested, but VK_EXT_debug_utils is unavailable");
    }
}

void VulkanContext::cleanup()
{
    if (m_isCleanedUp)
        return;

    if (m_device)
        VX_VK_TRY(vkDeviceWaitIdle(m_device));

    m_swapChain->cleanup();

    m_graphicsCommandPool->destroyVk();
    m_transferCommandPool->destroyVk();
    m_descriptorPool->destroyVk();

    if (m_device)
        m_device->clean();

    if (m_debugMessenger)
    {
        auto fpDestroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");

        if (fpDestroy)
            fpDestroy(m_instance, m_debugMessenger, nullptr);

        m_debugMessenger = VK_NULL_HANDLE;
    }

    if (m_surface)
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr), m_surface = VK_NULL_HANDLE;

    if (m_instance)
        vkDestroyInstance(m_instance, nullptr), m_instance = VK_NULL_HANDLE;

    m_isCleanedUp = true;
}

VkInstance VulkanContext::getInstance() const
{
    return m_instance;
}

VkSurfaceKHR VulkanContext::getSurface() const
{
    return m_surface;
}

VkPhysicalDevice VulkanContext::getPhysicalDevice() const
{
    return m_physicalDevice;
}

Device::SPtr VulkanContext::getDevice() const
{
    return m_device;
}

std::shared_ptr<SwapChain> VulkanContext::getSwapchain() const
{
    return m_swapChain;
}

VkQueue VulkanContext::getGraphicsQueue() const
{
    return m_graphicsQueue;
}

VkQueue VulkanContext::getPresentQueue() const
{
    return m_presentQueue;
}

VkQueue VulkanContext::getTransferQueue() const
{
    return m_transferQueue;
}

VkQueue VulkanContext::getComputeQueue() const
{
    return m_computeQueue;
}

CommandPool::SharedPtr VulkanContext::getTransferCommandPool() const
{
    return m_transferCommandPool;
}

CommandPool::SharedPtr VulkanContext::getGraphicsCommandPool() const
{
    return m_graphicsCommandPool;
}

ELIX_NESTED_NAMESPACE_END
