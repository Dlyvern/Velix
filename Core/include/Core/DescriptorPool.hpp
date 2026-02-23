#ifndef ELIX_DESCRIPTOR_POOL_HPP
#define ELIX_DESCRIPTOR_POOL_HPP

#include <vector>

#include "Core/Macros.hpp"

#include "volk.h"

ELIX_NESTED_NAMESPACE_BEGIN(core)

class DescriptorPool
{
    DECLARE_VK_HANDLE_METHODS(VkDescriptorPool)
    DECLARE_VK_SMART_PTRS(DescriptorPool, VkDescriptorPool)
    ELIX_DECLARE_VK_LIFECYCLE()

public:
    DescriptorPool(VkDevice device, const std::vector<VkDescriptorPoolSize> &poolSize, VkDescriptorPoolCreateFlags flags, uint32_t maxSets = 1000);
    void createVk(const std::vector<VkDescriptorPoolSize> &poolSize, VkDescriptorPoolCreateFlags flags, uint32_t maxSets = 1000);
    ~DescriptorPool();

private:
    VkDevice m_device{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_DESCRIPTOR_POOL_HPP