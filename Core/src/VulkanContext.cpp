#include "Core/VulkanContext.hpp"
#include "Core/VulkanAssert.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <iostream>
#include <cstring>
#include <set>
#include <limits>
#include <algorithm>
#include <unordered_map>

#include "Core/Memory/VMAAllocator.hpp"

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
    VX_VK_CHECK(volkInitialize());

    createInstance();
    createDebugger();
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();

    m_swapChain = SwapChain::create(window, m_surface, m_device, m_physicalDevice, m_queueFamilyIndices.graphicsFamily.value(), m_queueFamilyIndices.presentFamily.value());
}

void VulkanContext::createLogicalDevice()
{
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

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

    std::set<uint32_t> uniqueQueueFamilies =
        {
            m_queueFamilyIndices.graphicsFamily.value(),
            m_queueFamilyIndices.presentFamily.value(),
            m_queueFamilyIndices.computeFamily.value(),
            m_queueFamilyIndices.transferFamily.value()};

    float queuePriority{1.0f};

    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    deviceFeatures.independentBlend = VK_TRUE;
    deviceFeatures.imageCubeArray = VK_TRUE;

    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    v13.dynamicRendering = VK_TRUE;
    v13.synchronization2 = VK_TRUE;

    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.queueCreateInfoCount = queueCreateInfos.size();
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = m_deviceExtensions.size();
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = &v13;

    if (m_isValidationLayersEnabled)
    {
        createInfo.enabledLayerCount = m_validationLayers.size();
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }

    VX_VK_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_vkDevice));

    vkGetDeviceQueue(m_vkDevice, m_queueFamilyIndices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_vkDevice, m_queueFamilyIndices.presentFamily.value(), 0, &m_presentQueue);
    vkGetDeviceQueue(m_vkDevice, m_queueFamilyIndices.transferFamily.value(), 0, &m_transferQueue);
    vkGetDeviceQueue(m_vkDevice, m_queueFamilyIndices.computeFamily.value(), 0, &m_computeQueue);

    volkLoadDevice(m_vkDevice);

    m_transferCommandPool = CommandPool::createShared(m_vkDevice, m_queueFamilyIndices.transferFamily.value(),
                                                      VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    m_graphicsCommandPool = CommandPool::createShared(m_vkDevice, m_queueFamilyIndices.graphicsFamily.value(),
                                                      VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    std::vector<VkDescriptorPoolSize> descriptorPoolSizes = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

    m_descriptorPool = DescriptorPool::createShared(m_vkDevice, descriptorPoolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, 1000);

    VkPhysicalDeviceProperties selectedProperties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &selectedProperties);

    auto vmaAllocator = std::make_unique<allocators::VMAAllocator>(m_instance, m_physicalDevice, m_vkDevice, selectedProperties.apiVersion);
    m_device = Device::createShared(m_vkDevice, m_physicalDevice, std::move(vmaAllocator));
}

DescriptorPool::SharedPtr VulkanContext::getPersistentDescriptorPool() const
{
    return m_descriptorPool;
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

bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice device)
{
    uint32_t extensionCount{0};

    VX_VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr));

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);

    VX_VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data()));

    std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());

    for (const auto &extension : availableExtensions)
        requiredExtensions.erase(extension.extensionName);

    return requiredExtensions.empty();
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

    return indices.isComplete() && extensionsSupported && swapChainAdequate && features.samplerAnisotropy && features.imageCubeArray;
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
        VX_CORE_INFO_STREAM("GPU: " << candidate.props.deviceName << std::endl);
        VX_CORE_INFO_STREAM("Score: " << candidate.score << std::endl);
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
                                                  << ")\n");

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
        }
    }
    else
        createInfo.enabledLayerCount = 0;

    auto newExtensions = getRequiredExtensions();

    createInfo.enabledExtensionCount = newExtensions.size();
    createInfo.ppEnabledExtensionNames = newExtensions.data();

    VX_VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance));

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
#ifdef DEBUG_BUILD
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
        VX_VK_TRY(function(m_instance, &createInfo, nullptr, &m_debugMessenger));
#endif
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
