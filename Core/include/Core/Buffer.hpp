#ifndef ELIX_BUFFER_HPP
#define ELIX_BUFFER_HPP

#include <volk.h>
#include <memory>
#include <cstdint>

#include "Core/CommandPool.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/Macros.hpp"
#include "Core/Memory/Allocators.hpp"
#include "Core/Memory/MemoryFlags.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)

class Buffer
{
    DECLARE_VK_HANDLE_METHODS(VkBuffer)
    DECLARE_VK_SMART_PTRS(Buffer, VkBuffer)
    ELIX_DECLARE_VK_LIFECYCLE()
public:
    Buffer(VkDeviceSize size, VkBufferUsageFlags usage, memory::MemoryUsage memFlags, VkBufferCreateFlags flags = 0);

    void upload(const void* data, VkDeviceSize size);
    void upload(const void* data);

    void bind(VkDeviceSize memoryOffset = 0);

    void map(void*& data, VkDeviceSize offset = 0, VkMemoryMapFlags flags = 0);
    void map(void*& data, VkDeviceSize size, VkDeviceSize offset = 0, VkMemoryMapFlags flags = 0);

    void unmap();

    void createVk(VkDeviceSize size, VkBufferUsageFlags usage, memory::MemoryUsage memFlags, VkBufferCreateFlags flags = 0);

    static CommandBuffer copy(Ptr srcBuffer, Ptr dstBuffer, CommandPool::SharedPtr commandPool,  VkDeviceSize size);
    
    static SharedPtr createCopied(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, memory::MemoryUsage memFlags, CommandPool::SharedPtr commandPool = nullptr);

    ~Buffer();
private:
    allocators::AllocatedBuffer m_bufferAllocation;
    VkDeviceSize m_size;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_BUFFER_HPP