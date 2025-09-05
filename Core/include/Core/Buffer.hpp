#ifndef ELIX_BUFFER_HPP
#define ELIX_BUFFER_HPP

#include "Macros.hpp"
#include <volk.h>
#include <memory>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class Buffer
{
public:
    Buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags);

    void upload(const void* data, VkDeviceSize size);

    static std::shared_ptr<Buffer> create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags);

    VkBuffer vkBuffer();
    VkDeviceMemory vkDeviceMemory();

    ~Buffer();
private:
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags flags);

    VkBuffer m_buffer{VK_NULL_HANDLE};
    VkDeviceMemory m_bufferMemory{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_BUFFER_HPP