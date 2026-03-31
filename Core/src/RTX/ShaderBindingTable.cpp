#include "Core/RTX/ShaderBindingTable.hpp"

#include "Core/VulkanContext.hpp"

#include <cstring>

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(rtx)

namespace
{
    VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
    {
        if (alignment == 0u)
            return value;

        return (value + alignment - 1u) & ~(alignment - 1u);
    }

    VkDeviceAddress getBufferDeviceAddress(const core::Buffer &buffer)
    {
        auto context = core::VulkanContext::getContext();
        if (!context)
            return 0u;

        VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        addressInfo.buffer = buffer.vk();
        return vkGetBufferDeviceAddress(context->getDevice(), &addressInfo);
    }

    struct SectionLayout
    {
        VkStridedDeviceAddressRegionKHR region{};
        VkDeviceSize offset{0u};
        VkDeviceSize stride{0u};
        VkDeviceSize size{0u};
    };

    SectionLayout makeSectionLayout(const std::vector<uint32_t> &groupIndices,
                                    VkDeviceSize currentOffset,
                                    VkDeviceSize handleSizeAligned,
                                    VkDeviceSize baseAlignment,
                                    bool isRaygen)
    {
        SectionLayout layout{};
        layout.offset = currentOffset;

        if (groupIndices.empty())
            return layout;

        layout.stride = isRaygen ? alignUp(handleSizeAligned, baseAlignment) : handleSizeAligned;
        layout.size = alignUp(layout.stride * groupIndices.size(), baseAlignment);
        layout.region.stride = layout.stride;
        layout.region.size = layout.size;
        return layout;
    }
}

ShaderBindingTable::SharedPtr ShaderBindingTable::create(const RayTracingPipeline &pipeline,
                                                         const std::vector<uint32_t> &raygenGroups,
                                                         const std::vector<uint32_t> &missGroups,
                                                         const std::vector<uint32_t> &hitGroups,
                                                         const std::vector<uint32_t> &callableGroups)
{
    auto sbt = std::shared_ptr<ShaderBindingTable>(new ShaderBindingTable());
    if (!sbt->createInternal(pipeline, raygenGroups, missGroups, hitGroups, callableGroups))
        return nullptr;

    return sbt;
}

bool ShaderBindingTable::createInternal(const RayTracingPipeline &pipeline,
                                        const std::vector<uint32_t> &raygenGroups,
                                        const std::vector<uint32_t> &missGroups,
                                        const std::vector<uint32_t> &hitGroups,
                                        const std::vector<uint32_t> &callableGroups)
{
    auto context = core::VulkanContext::getContext();
    if (!context || !pipeline.isValid() || pipeline.groupCount() == 0u || raygenGroups.empty())
        return false;

    const uint32_t groupCount = pipeline.groupCount();
    auto validateGroups = [groupCount](const std::vector<uint32_t> &groups)
    {
        for (const uint32_t groupIndex : groups)
            if (groupIndex >= groupCount)
                return false;
        return true;
    };

    if (!validateGroups(raygenGroups) || !validateGroups(missGroups) ||
        !validateGroups(hitGroups) || !validateGroups(callableGroups))
        return false;

    const auto &rtProperties = context->getRayTracingPipelineProperties();
    const VkDeviceSize handleSize = rtProperties.shaderGroupHandleSize;
    const VkDeviceSize handleAlignment = rtProperties.shaderGroupHandleAlignment;
    const VkDeviceSize baseAlignment = rtProperties.shaderGroupBaseAlignment;
    const VkDeviceSize handleSizeAligned = alignUp(handleSize, handleAlignment);

    SectionLayout raygenLayout = makeSectionLayout(raygenGroups, 0u, handleSizeAligned, baseAlignment, true);
    SectionLayout missLayout = makeSectionLayout(missGroups, raygenLayout.offset + raygenLayout.size, handleSizeAligned, baseAlignment, false);
    SectionLayout hitLayout = makeSectionLayout(hitGroups, missLayout.offset + missLayout.size, handleSizeAligned, baseAlignment, false);
    SectionLayout callableLayout = makeSectionLayout(callableGroups, hitLayout.offset + hitLayout.size, handleSizeAligned, baseAlignment, false);

    const VkDeviceSize sbtSize = callableLayout.offset + callableLayout.size;
    if (sbtSize == 0u)
        return false;

    m_buffer = core::Buffer::createShared(
        sbtSize,
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        core::memory::MemoryUsage::CPU_TO_GPU);

    if (!m_buffer)
        return false;

    std::vector<uint8_t> shaderHandleStorage(static_cast<size_t>(groupCount) * handleSize);
    if (vkGetRayTracingShaderGroupHandlesKHR(context->getDevice(),
                                             pipeline.vk(),
                                             0u,
                                             groupCount,
                                             shaderHandleStorage.size(),
                                             shaderHandleStorage.data()) != VK_SUCCESS)
    {
        m_buffer.reset();
        return false;
    }

    void *mappedData = nullptr;
    m_buffer->map(mappedData);
    auto *dst = static_cast<uint8_t *>(mappedData);

    auto copySection = [&](const std::vector<uint32_t> &groups, const SectionLayout &layout)
    {
        for (size_t index = 0; index < groups.size(); ++index)
        {
            std::memcpy(dst + layout.offset + layout.stride * index,
                        shaderHandleStorage.data() + handleSize * groups[index],
                        handleSize);
        }
    };

    copySection(raygenGroups, raygenLayout);
    copySection(missGroups, missLayout);
    copySection(hitGroups, hitLayout);
    copySection(callableGroups, callableLayout);

    m_buffer->unmap();

    const VkDeviceAddress baseAddress = getBufferDeviceAddress(*m_buffer);
    if (baseAddress == 0u)
    {
        m_buffer.reset();
        return false;
    }

    m_raygenRegion = raygenLayout.region;
    m_missRegion = missLayout.region;
    m_hitRegion = hitLayout.region;
    m_callableRegion = callableLayout.region;

    if (m_raygenRegion.size > 0u)
        m_raygenRegion.deviceAddress = baseAddress + raygenLayout.offset;
    if (m_missRegion.size > 0u)
        m_missRegion.deviceAddress = baseAddress + missLayout.offset;
    if (m_hitRegion.size > 0u)
        m_hitRegion.deviceAddress = baseAddress + hitLayout.offset;
    if (m_callableRegion.size > 0u)
        m_callableRegion.deviceAddress = baseAddress + callableLayout.offset;

    return true;
}

void ShaderBindingTable::destroy()
{
    m_buffer.reset();
    m_raygenRegion = {};
    m_missRegion = {};
    m_hitRegion = {};
    m_callableRegion = {};
}

ShaderBindingTable::~ShaderBindingTable()
{
    destroy();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
