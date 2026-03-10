#include "Engine/RayTracing/AccelerationStructure.hpp"

#include "Core/VulkanContext.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(rayTracing)

AccelerationStructure::SharedPtr AccelerationStructure::create(VkAccelerationStructureTypeKHR type, VkDeviceSize size)
{
    auto accelerationStructure = std::shared_ptr<AccelerationStructure>(new AccelerationStructure());
    if (!accelerationStructure->createInternal(type, size))
        return nullptr;

    return accelerationStructure;
}

bool AccelerationStructure::createInternal(VkAccelerationStructureTypeKHR type, VkDeviceSize size)
{
    auto context = core::VulkanContext::getContext();
    if (!context || !context->hasAccelerationStructureSupport() || size == 0u)
        return false;

    m_type = type;
    m_size = size;

    m_buffer = core::Buffer::createShared(
        size,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        core::memory::MemoryUsage::GPU_ONLY);

    if (!m_buffer)
        return false;

    VkAccelerationStructureCreateInfoKHR createInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    createInfo.buffer = m_buffer->vk();
    createInfo.offset = 0u;
    createInfo.size = size;
    createInfo.type = type;

    if (vkCreateAccelerationStructureKHR(context->getDevice(), &createInfo, nullptr, &m_handle) != VK_SUCCESS)
    {
        m_buffer.reset();
        m_handle = VK_NULL_HANDLE;
        return false;
    }

    refreshDeviceAddress();
    return m_deviceAddress != 0u;
}

void AccelerationStructure::refreshDeviceAddress()
{
    auto context = core::VulkanContext::getContext();
    if (!context || m_handle == VK_NULL_HANDLE)
    {
        m_deviceAddress = 0u;
        return;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addressInfo.accelerationStructure = m_handle;
    m_deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(context->getDevice(), &addressInfo);
}

void AccelerationStructure::destroy()
{
    if (m_handle != VK_NULL_HANDLE)
    {
        auto context = core::VulkanContext::getContext();
        if (context)
            vkDestroyAccelerationStructureKHR(context->getDevice(), m_handle, nullptr);
        m_handle = VK_NULL_HANDLE;
    }

    m_buffer.reset();
    m_deviceAddress = 0u;
    m_size = 0u;
}

AccelerationStructure::~AccelerationStructure()
{
    destroy();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
