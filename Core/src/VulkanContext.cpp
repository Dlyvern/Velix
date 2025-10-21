#include "Core/VulkanContext.hpp"

#define GGLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <iostream>
#include <cstring>
#include <set>
#include <limits>
#include <algorithm>

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    if(messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << (std::string(pCallbackData->pMessage)) << std::endl;
    else if(messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
        std::cout << (std::string(pCallbackData->pMessage)) << std::endl;
    else if(messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        std::cout << (std::string(pCallbackData->pMessage)) << std::endl;
    else if(messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        std::cerr << (std::string(pCallbackData->pMessage)) << std::endl;

    return VK_FALSE;
}

ELIX_NESTED_NAMESPACE_BEGIN(core)

static std::shared_ptr<VulkanContext> GLOBAL_VULKAN_CONTEXT{nullptr};

std::shared_ptr<VulkanContext> VulkanContext::create(std::shared_ptr<platform::Window> window)
{
    if(GLOBAL_VULKAN_CONTEXT)
        return GLOBAL_VULKAN_CONTEXT;
    
    GLOBAL_VULKAN_CONTEXT = std::make_shared<VulkanContext>(window);

    return GLOBAL_VULKAN_CONTEXT;
}

std::shared_ptr<VulkanContext> VulkanContext::getContext()
{
    if(!GLOBAL_VULKAN_CONTEXT)
        throw std::runtime_error("NO VULKAN CONTEXT CREATED");
    
    return GLOBAL_VULKAN_CONTEXT;
}

VulkanContext::VulkanContext(std::shared_ptr<platform::Window> window)
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
    if(!m_isCleanedUp)
    {
        std::cerr << "ERROR: VulkanContext destroyed without calling cleanup()!!!" << std::endl;
        //DO NOT CALL cleanup here. It is too late...
    }
}

void VulkanContext::initVulkan(std::shared_ptr<platform::Window> window)
{
    if(volkInitialize() != VK_SUCCESS)
        throw std::runtime_error("Failed to initialize volk");

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

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

    std::set<uint32_t> uniqueQueueFamilies = {m_queueFamilyIndices.graphicsFamily.value(), m_queueFamilyIndices.presentFamily.value()};

    float queuePriority{1.0f};

    for(uint32_t queueFamily : uniqueQueueFamilies)
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

    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.queueCreateInfoCount = queueCreateInfos.size();
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = m_deviceExtensions.size();
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();
    createInfo.enabledLayerCount = 0;

    if(m_isValidationLayersEnabled)
    {
        createInfo.enabledLayerCount = m_validationLayers.size();
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }

    if(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
        throw std::runtime_error("Failed to create logical device");

    vkGetDeviceQueue(m_device, m_queueFamilyIndices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_queueFamilyIndices.presentFamily.value(), 0, &m_presentQueue);
    
    volkLoadDevice(m_device);
}

const VulkanContext::QueueFamilyIndices& VulkanContext::getQueueFamilyIndices() const
{
    return m_queueFamilyIndices;
}

VulkanContext::QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount{0};

    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamiles(queueFamilyCount);

    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamiles.data());

    int i{0};
    VkBool32 presentSupport{false};

    for(const auto& queueFamily : queueFamiles)
    {
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

        if(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.graphicsFamily = i;

        if(presentSupport)
            indices.presentFamily = i;

        ++i;
    }

    return indices;
}

VulkanContext::SwapChainSupportDetails VulkanContext::querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount{0};

    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if(formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount{0};

    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if(presentModeCount != 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkPhysicalDeviceFeatures VulkanContext::getPhysicalDeviceFeatures()
{
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &features);
    return features;
}

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device)
{
    QueueFamilyIndices indices = findQueueFamilies(device, m_surface);

    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate{false};

    if(extensionsSupported)
    {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device, m_surface);

        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(device, &features);

    return indices.isComplete() && extensionsSupported && swapChainAdequate && features.samplerAnisotropy;
}

void VulkanContext::pickPhysicalDevice()
{
    uint32_t deviceCount{0};

    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if(deviceCount == 0)
        throw std::runtime_error("Failed to find GPUs with Vulkan support");

    std::vector<VkPhysicalDevice> devices(deviceCount);

    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    for(const auto& device : devices)
    {
        if(isDeviceSuitable(device))
        {
            m_physicalDevice = device;
            break;
        }
    }

    if(!m_physicalDevice)
        throw std::runtime_error("Failed to find a suitable GPU");
}

void VulkanContext::createSurface(std::shared_ptr<platform::Window> window)
{
    if(glfwCreateWindowSurface(m_instance, window->getRawHandler(), nullptr, &m_surface) != VK_SUCCESS)
        throw std::runtime_error("Failed to create window surface");
}

void VulkanContext::createInstance()
{
    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "VelixCore";
    applicationInfo.pEngineName = "VelixEngine";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    applicationInfo.engineVersion = VK_MAKE_VERSION(0, 0, 1);
    applicationInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t glfwExtensionCount{0};

    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    uint32_t extensionCount{0};    

    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> extensions(extensionCount);

    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

    //TODO check if all glfw extensions are represented in vulkan extensions
    for(const auto& extension : extensions)
        std::cout << ("Vulkan extension: ") << extension.extensionName << std::endl;  

    for(int index = 0; index < glfwExtensionCount; ++index)
        std::cout << ("GLFW extension: ") << glfwExtensions[index] << std::endl;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;

    if(m_isValidationLayersEnabled)
    {
        if(!checkValidationLayers())
            std::cerr << ("Validation layers required, but not available") << std::endl;
        else
        {
            createInfo.enabledLayerCount = m_validationLayers.size();
            createInfo.ppEnabledLayerNames = m_validationLayers.data();
        }
    }
    else
        createInfo.enabledLayerCount = 0;

    auto newExtensions = getRequiredExtensions();

    createInfo.enabledExtensionCount = newExtensions.size();
    createInfo.ppEnabledExtensionNames = newExtensions.data();

    if(vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
        throw std::runtime_error("Failed to create Vulkan instance");

    volkLoadInstance(m_instance);
}

bool VulkanContext::checkValidationLayers()
{
    uint32_t layerCount;

    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);

    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    bool layerFound{false};

    for(const auto& layerName : m_validationLayers)
    {
        for(const auto& availableLayer : availableLayers)
        {
            if(strcmp(layerName, availableLayer.layerName) == 0)
            {
                layerFound = true;
                break;
            }
        }
    }

    return layerFound;
}

bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice device)
{
    uint32_t extensionCount{0};

    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);

    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());

    for(const auto& extension : availableExtensions)
        requiredExtensions.erase(extension.extensionName);

    return requiredExtensions.empty();
}

VkPhysicalDeviceProperties VulkanContext::getPhysicalDevicePoperties()
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    return properties;
}

std::vector<const char*> VulkanContext::getRequiredExtensions()
{
    uint32_t glfwExtensionCount{0};

    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if(m_isValidationLayersEnabled)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    return extensions;
}

void VulkanContext::createDebugger()
{
#ifdef DEBUG_BUILD
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};

    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
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
        if (function(m_instance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS)
            std::cerr << ("Failed to create debug messenger") << std::endl;
#endif
}

void VulkanContext::cleanup()
{
    if(m_isCleanedUp)
        return;

    if (m_device)
        vkDeviceWaitIdle(m_device);

    m_swapChain->cleanup();

    if (m_device)
        vkDestroyDevice(m_device, nullptr), m_device = VK_NULL_HANDLE;

    if (m_debugMessenger)
    {
        auto fpDestroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");\

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

VkDevice VulkanContext::getDevice() const
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

ELIX_NESTED_NAMESPACE_END