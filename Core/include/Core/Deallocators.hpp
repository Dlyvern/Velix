#ifndef ELIX_DEALLOCATORS_HPP
#define ELIX_DEALLOCATORS_HPP

#include "Core/Macros.hpp"

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(deallocators)

class ImageDelete
{
public:
    void operator()(VkImage image, VkDeviceMemory memory, VkDevice device, const VkAllocationCallbacks* allocationCallbacks = nullptr) const noexcept
    {
        if(image)
        {
            vkDestroyImage(device, image, allocationCallbacks);
            image = VK_NULL_HANDLE;
        }
        if(memory)
        {
            vkFreeMemory(device, memory, allocationCallbacks);
            memory = VK_NULL_HANDLE;
        }
    }
};

class ImageNoDelete
{
public:
    void operator()(VkImage image, VkDeviceMemory memory, VkDevice device, const VkAllocationCallbacks* allocationCallbacks = nullptr) const noexcept
    {

    }
};


ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif //ELIX_DEALLOCATORS_HPP