#ifndef ELIX_CORE_RTX_ACCELERATION_STRUCTURE_HPP
#define ELIX_CORE_RTX_ACCELERATION_STRUCTURE_HPP

#include "Core/Buffer.hpp"
#include "Core/Macros.hpp"

#include <volk.h>

#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(rtx)

class AccelerationStructure
{
    DECLARE_VK_HANDLE_METHODS(VkAccelerationStructureKHR)
    ELIX_DECLARE_VK_LIFECYCLE()
public:
    using SharedPtr = std::shared_ptr<AccelerationStructure>;

    static SharedPtr create(VkAccelerationStructureTypeKHR type, VkDeviceSize size);

    AccelerationStructure(const AccelerationStructure &) = delete;
    AccelerationStructure &operator=(const AccelerationStructure &) = delete;

    ~AccelerationStructure();

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

private:
    AccelerationStructure() = default;

    bool createInternal(VkAccelerationStructureTypeKHR type, VkDeviceSize size);
    void refreshDeviceAddress();

    VkDevice m_device{VK_NULL_HANDLE};
    core::Buffer::SharedPtr m_buffer{nullptr};
    VkDeviceAddress m_deviceAddress{0u};
    VkAccelerationStructureTypeKHR m_type{VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR};
    VkDeviceSize m_size{0u};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif
