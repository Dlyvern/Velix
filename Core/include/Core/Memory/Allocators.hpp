#ifndef ELIX_ALLOCATORS_HPP
#define ELIX_ALLOCATORS_HPP

#include "Core/Macros.hpp"

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(allocators)

struct AllocatedBuffer
{
    VkBuffer buffer{VK_NULL_HANDLE};
    void* allocation{nullptr};

    operator VkBuffer() const 
    {
        return buffer; 
    }
};

struct AllocatedImage
{
    VkImage image{VK_NULL_HANDLE};
    void* allocation{nullptr};

    operator VkImage() const 
    {
        return image; 
    }
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif //ELIX_ALLOCATORS_HPP