#ifndef ELIX_VULKAN_HELPERS_HPP
#define ELIX_VULKAN_HELPERS_HPP

#include "Core/Macros.hpp"
#include "Core/VulkanContext.hpp"

#include <cstdint>
#include <iostream>

//!!!! FOR PRODUCTION THIS FILE IS NOT SUITABLE. FIX IT

ELIX_NESTED_NAMESPACE_BEGIN(core)

namespace helpers
{
    inline uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags flags)
    {
        VkPhysicalDeviceMemoryProperties memoryProperties;
        vkGetPhysicalDeviceMemoryProperties(VulkanContext::getContext()->getPhysicalDevice(), &memoryProperties);

        for(uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
            if((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & flags) == flags)
                return i;

        std::cerr << "Failed to find suitable memory type" << std::endl;

        return 0xFFFFFFFF;
    }

    inline VkFormat findSupportedFormat(const std::vector<VkFormat>&candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
    {
        for(auto format : candidates)
        {
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(VulkanContext::getContext()->getPhysicalDevice(), format, &properties);

            if(tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & features) == features)
                return format;
            else if(tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features)
                return format;
        }

        std::cerr << "Failed to fund suitable supported format" << std::endl;

        return VK_FORMAT_UNDEFINED;
    }

    inline VkFormat findDepthFormat()
    {
        auto format = findSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

        if(format == VK_FORMAT_UNDEFINED)
            std::cerr << "Failed to fund depth format" << std::endl;

        return format;
    }

    inline bool hasStencilComponent(VkFormat format)
    {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
    }
} //namespace helpers

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_VULKAN_HELPERS_HPP