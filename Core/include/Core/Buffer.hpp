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

    Buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memFlags, VkBufferCreateFlags flags = 0);

    void upload(const void* data, VkDeviceSize size);

    void bind(VkDeviceSize memoryOffset = 0);

    void map(void*& data, VkDeviceSize offset = 0, VkMemoryMapFlags flags = 0);
    void map(VkDeviceSize offset, VkDeviceSize size,  VkMemoryMapFlags flags, void*& data);

    void unmap();

    void destroyVk();

    static SharedPtr create(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memFlags, VkBufferCreateFlags flags = 0);

    static CommandBuffer::SharedPtr copy(SharedPtr srcBuffer,  SharedPtr dstBuffer, CommandPool::SharedPtr commandPool,  VkDeviceSize size);
    
    static SharedPtr createCopied(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memFlags, CommandPool::SharedPtr commandPool, VkQueue queue);

    VkBuffer vkBuffer();
    VkDeviceMemory vkDeviceMemory();

    ~Buffer();
private:
    VkBuffer m_buffer{VK_NULL_HANDLE};
    VkDeviceMemory m_bufferMemory{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    VkDeviceSize m_size;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_BUFFER_HPP