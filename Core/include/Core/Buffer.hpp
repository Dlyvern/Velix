#ifndef ELIX_BUFFER_HPP
#define ELIX_BUFFER_HPP

#include "Macros.hpp"
#include <volk.h>
#include <memory>
#include <cstdint>

#include "Core/CommandPool.hpp"
#include "Core/CommandBuffer.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)

class Buffer
{
public:
    using SharedPtr = std::shared_ptr<Buffer>;

    Buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, VkMemoryPropertyFlags memFlags);

    void upload(const void* data, VkDeviceSize size);

    void bind(VkDeviceSize memoryOffset = 0);

    void destroy();

    static SharedPtr create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, VkMemoryPropertyFlags memFlags);

    static CommandBuffer::SharedPtr copy(SharedPtr srcBuffer,  SharedPtr dstBuffer, CommandPool::SharedPtr commandPool,  VkDeviceSize size);
    
    static SharedPtr createCopied(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, VkMemoryPropertyFlags memFlags, CommandPool::SharedPtr commandPool, VkQueue queue);

    VkBuffer vkBuffer();
    VkDeviceMemory vkDeviceMemory();

    ~Buffer();
private:
    bool m_isDestroyed{false};
    VkBuffer m_buffer{VK_NULL_HANDLE};
    VkDeviceMemory m_bufferMemory{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_BUFFER_HPP