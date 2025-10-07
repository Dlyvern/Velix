#ifndef ELIX_UNIFORM_BUFFER_OBJECT_HPP
#define ELIX_UNIFORM_BUFFER_OBJECT_HPP

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"

#include <cstring>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

template<typename T>
class UniformBufferObject
{
public:
    using SharedPtr = std::shared_ptr<UniformBufferObject>;

    constexpr VkDeviceSize getDeviceSize() const
    {
        return sizeof(T);
    }

    explicit UniformBufferObject(VkDevice device)
    {
        m_buffer = core::Buffer::create(sizeof(T), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 0,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        vkMapMemory(device, m_buffer->vkDeviceMemory(), 0, sizeof(T), 0, &m_mapped);
    }

    ~UniformBufferObject() = default;

    static SharedPtr create(VkDevice device)
    {
        return std::make_shared<UniformBufferObject>(device);
    }

    void update(const void* data)
    {
        std::memcpy(m_mapped, data, sizeof(T));
    }

    core::Buffer::SharedPtr getBuffer() const
    {
        return m_buffer;
    }

private:
    core::Buffer::SharedPtr m_buffer{nullptr};

    //TODO do we need to free it?
    void* m_mapped{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_UNIFORM_BUFFER_OBJECT_HPP