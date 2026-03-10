#ifndef ELIX_ACCELERATION_STRUCTURE_HPP
#define ELIX_ACCELERATION_STRUCTURE_HPP

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"

#include <volk.h>

#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(rayTracing)

class AccelerationStructure
{
public:
    using SharedPtr = std::shared_ptr<AccelerationStructure>;

    static SharedPtr create(VkAccelerationStructureTypeKHR type, VkDeviceSize size);

    ~AccelerationStructure();

    VkAccelerationStructureKHR vk() const
    {
        return m_handle;
    }

    VkDeviceAddress deviceAddress() const
    {
        return m_deviceAddress;
    }

    VkDeviceSize size() const
    {
        return m_size;
    }

    VkAccelerationStructureTypeKHR type() const
    {
        return m_type;
    }

    const core::Buffer::SharedPtr &buffer() const
    {
        return m_buffer;
    }

    bool isValid() const
    {
        return m_handle != VK_NULL_HANDLE;
    }

    void destroy();

private:
    AccelerationStructure() = default;

    bool createInternal(VkAccelerationStructureTypeKHR type, VkDeviceSize size);
    void refreshDeviceAddress();

    VkAccelerationStructureKHR m_handle{VK_NULL_HANDLE};
    core::Buffer::SharedPtr m_buffer{nullptr};
    VkDeviceAddress m_deviceAddress{0u};
    VkAccelerationStructureTypeKHR m_type{VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR};
    VkDeviceSize m_size{0u};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ACCELERATION_STRUCTURE_HPP
