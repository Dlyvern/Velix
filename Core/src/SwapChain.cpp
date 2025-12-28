#include "Core/SwapChain.hpp"

#include <GLFW/glfw3.h>
#include "Core/VulkanContext.hpp"

#include <limits>
#include <iostream>
#include <array>
#include <algorithm>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

SwapChain::SwapChain(platform::Window::SharedPtr window, VkSurfaceKHR surface, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t graphicsFamily, uint32_t presentFamily) :
m_device(device), m_physicalDevice(physicalDevice), m_window(window), m_surface(surface), m_graphicsFamily(graphicsFamily), m_presentFamily(presentFamily)
{
    createSwapChain();
}

void SwapChain::createSwapChain()
{   
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(m_physicalDevice, m_surface);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities, m_window);

    m_imageCount = swapChainSupport.capabilities.minImageCount + 1;
    
    if(swapChainSupport.capabilities.maxImageCount > 0 && m_imageCount > swapChainSupport.capabilities.maxImageCount)
        m_imageCount = swapChainSupport.capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    createInfo.surface = m_surface;

    createInfo.minImageCount = m_imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndicesArr[] = {m_graphicsFamily, m_presentFamily};

    if(m_graphicsFamily != m_presentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndicesArr;
    }
    else
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if(vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain) != VK_SUCCESS)
        throw std::runtime_error("Failed to create swap chain");

    uint32_t swapchainCount;
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &swapchainCount, nullptr);
    m_swapChainImages.resize(swapchainCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &swapchainCount, m_swapChainImages.data());

    m_imageFormat = surfaceFormat.format;
    m_extent = extent;
}

SwapChain::SwapChainSupportDetails SwapChain::querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
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

const std::vector<VkImage>& SwapChain::getImages() const
{
    return m_swapChainImages;
}

void SwapChain::recreate()
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window->getRawHandler(), &width, &height);

    while(width == 0 || height == 0)
    {
        glfwGetFramebufferSize(m_window->getRawHandler(), &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_device);

    cleanup();

    createSwapChain();
}

VkViewport SwapChain::getViewport(float x, float y, float minDepth, float maxDepth)
{
    return VkViewport{x, y, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), minDepth, maxDepth};
}

VkRect2D SwapChain::getScissor(VkOffset2D offset)
{
    return VkRect2D{offset, m_extent};
}

std::shared_ptr<platform::Window> SwapChain::getWindow()
{
    return m_window;
}

uint32_t SwapChain::getImageCount() const
{
    return m_imageCount;
}

SwapChain::SharedPtr SwapChain::create(std::shared_ptr<platform::Window> window, VkSurfaceKHR surface, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t graphicsFamily, uint32_t presentFamily)
{
    return std::make_shared<SwapChain>(window, surface, device, physicalDevice, graphicsFamily, presentFamily);
}

VkSurfaceFormatKHR SwapChain::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
    for(const auto& availableFormat : availableFormats)
        if(availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return availableFormat;

    return availableFormats[0];
}

VkPresentModeKHR SwapChain::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
{
    for(const auto& availablePresentMode : availablePresentModes)
        if(availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            return availablePresentMode;

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, std::shared_ptr<platform::Window> window)
{
    if(capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return capabilities.currentExtent;

    int width, height;

    glfwGetFramebufferSize(window->getRawHandler(), &width, &height);

    VkExtent2D actualExtent{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

void SwapChain::cleanup()
{
    if (m_swapChain)
    {
        vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
}

VkSwapchainKHR SwapChain::vk() const
{
    return m_swapChain;
}

VkExtent2D SwapChain::getExtent() const
{
    return m_extent;
}

VkFormat SwapChain::getImageFormat() const
{
    return m_imageFormat;
}

SwapChain::~SwapChain()
{
    cleanup();
}

ELIX_NESTED_NAMESPACE_END