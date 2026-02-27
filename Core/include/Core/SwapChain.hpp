#ifndef ELIX_SWAP_CHAIN_HPP
#define ELIX_SWAP_CHAIN_HPP

#include "Core/Window.hpp"
#include "Core/Macros.hpp"

#include <vector>

#include <volk.h>

#include <cstdint>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class SwapChain
{
public:
    using SharedPtr = std::shared_ptr<SwapChain>;

    SwapChain(platform::Window &window, VkSurfaceKHR surface, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t graphicsFamily, uint32_t presentFamily);
    static SharedPtr create(platform::Window &window, VkSurfaceKHR surface, VkDevice device, VkPhysicalDevice physicalDevice, uint32_t graphicsFamily, uint32_t presentFamily);

    VkSwapchainKHR vk() const;
    VkExtent2D getExtent() const;
    VkFormat getImageFormat() const;
    uint32_t getImageCount() const;
    platform::Window &getWindow() const;
    VkViewport getViewport(float x = 0.0f, float y = 0.0f, float minDepth = 0.0f, float maxDepth = 1.0f);
    VkRect2D getScissor(VkOffset2D offset = {0, 0});
    const std::vector<VkImage>& getImages() const;

    void recreate();

    void cleanup();

    ~SwapChain();
private:
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, platform::Window &window);

    struct SwapChainSupportDetails
    {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

    void createSwapChain();
    VkDevice m_device{VK_NULL_HANDLE};  
    VkSwapchainKHR m_swapChain{VK_NULL_HANDLE};
    VkFormat m_imageFormat{};
    VkExtent2D m_extent{};
    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    platform::Window *m_window{nullptr};

    VkSurfaceKHR m_surface{VK_NULL_HANDLE};

    uint32_t m_graphicsFamily{0};
    uint32_t m_presentFamily{0};

    std::vector<VkImage> m_swapChainImages;

    uint32_t m_imageCount;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_SWAP_CHAIN_HPP
