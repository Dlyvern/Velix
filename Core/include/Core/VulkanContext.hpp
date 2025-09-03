#ifndef VULKAN_CONTEXT_HPP
#define VULKAN_CONTEXT_HPP

#include "Macros.hpp"
#include "Window.hpp"

#include <volk.h>

#include <vector>
#include <string>
#include <cstdint>
#include <optional>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class VulkanContext
{
public:
    explicit VulkanContext(std::shared_ptr<platform::Window> window);
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    static std::shared_ptr<VulkanContext> create(std::shared_ptr<platform::Window> window);
    static std::shared_ptr<VulkanContext> getContext();

    VkInstance getInstance() const;
    VkSurfaceKHR getSurface() const;
    VkPhysicalDevice getPhysicalDevice() const;
    VkDevice getDevice() const;
    VkSwapchainKHR getSwapchain() const;
    VkQueue getGraphicsQueue() const;
    VkQueue getPresentQueue() const;
    VkFormat getSwapchainImageFormat() const;
    VkExtent2D getSwapchainExtent() const;

    const std::vector<VkImageView>& getSwapChainImageViews() const;

    //TODO FIX THAT(we need it for Render class)
    struct QueueFamilyIndices
    {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() const
        {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

    void cleanup();
    
    ~VulkanContext();

private:
    bool m_isCleanedUp{false};

    struct SwapChainSupportDetails
    {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    std::shared_ptr<platform::Window> m_window{nullptr};

    bool m_isValidationLayersEnabled{true};

    uint32_t m_graphicsQueueFamilyIndex{0};

    const std::vector<const char*> m_validationLayers{"VK_LAYER_KHRONOS_validation"};
    const std::vector<const char*> m_deviceExtensions
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#ifdef __APPLE__
        "VK_KHR_portability_subset",
#endif
    };

    std::vector<VkImage> m_swapChainImages;
    std::vector<VkImageView> m_swapChainImageViews;

    VkInstance m_instance{VK_NULL_HANDLE};
    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_debugMessenger{VK_NULL_HANDLE};
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
    VkQueue m_graphicsQueue{VK_NULL_HANDLE};
    VkQueue m_presentQueue{VK_NULL_HANDLE};
    VkSwapchainKHR m_swapChain{VK_NULL_HANDLE};
    VkFormat m_swapChainImageFormat{};
    VkExtent2D m_swapChainExtent{};

    bool checkValidationLayers();
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    std::vector<const char*> getRequiredExtensions();
    void pickPhysicalDevice();
    bool isDeviceSuitable(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, std::shared_ptr<platform::Window> window);

    void initVulkan(std::shared_ptr<platform::Window> window);
    void createInstance();
    void createDebugger();
    void createSurface(std::shared_ptr<platform::Window> window);
    void createLogicalDevice();
    void createSwapChain(std::shared_ptr<platform::Window> window);
    void createImageViews();
};

ELIX_NESTED_NAMESPACE_END

#endif //VULKAN_CONTEXT_HPP