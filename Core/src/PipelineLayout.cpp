#include "Core/PipelineLayout.hpp"
#include "Core/VulkanAssert.hpp"
#include <cstdint>
#include <string>

namespace
{
    PFN_vkCreatePipelineLayout resolveCreatePipelineLayoutProc(VkDevice device)
    {
        if (device == VK_NULL_HANDLE)
            throw std::runtime_error("PipelineLayout::createVk received VK_NULL_HANDLE device");

        auto *createPipelineLayout = reinterpret_cast<PFN_vkCreatePipelineLayout>(
            vkGetDeviceProcAddr(device, "vkCreatePipelineLayout"));
        if (!createPipelineLayout)
            throw std::runtime_error("Failed to resolve vkCreatePipelineLayout via vkGetDeviceProcAddr");

        return createPipelineLayout;
    }

    PFN_vkDestroyPipelineLayout resolveDestroyPipelineLayoutProc(VkDevice device)
    {
        if (device == VK_NULL_HANDLE)
            return nullptr;

        return reinterpret_cast<PFN_vkDestroyPipelineLayout>(
            vkGetDeviceProcAddr(device, "vkDestroyPipelineLayout"));
    }
}

ELIX_NESTED_NAMESPACE_BEGIN(core)

PipelineLayout::PipelineLayout(VkDevice device, const std::vector<std::reference_wrapper<const DescriptorSetLayout>> &setLayouts, const std::vector<VkPushConstantRange> &pushConstants)
    : m_device(device)
{
    createVk(setLayouts, pushConstants);
}

void PipelineLayout::createVk(const std::vector<std::reference_wrapper<const DescriptorSetLayout>> &setLayouts, const std::vector<VkPushConstantRange> &pushConstants)
{
    ELIX_VK_CREATE_GUARD()

    std::vector<VkDescriptorSetLayout> vkSetLayouts;
    vkSetLayouts.reserve(setLayouts.size());

    for (const auto &des : setLayouts)
        vkSetLayouts.push_back(des.get().vk());

    for (size_t index = 0; index < vkSetLayouts.size(); ++index)
    {
        if (vkSetLayouts[index] == VK_NULL_HANDLE)
            throw std::runtime_error("PipelineLayout::createVk received VK_NULL_HANDLE descriptor set layout at index " + std::to_string(index));
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(vkSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = vkSetLayouts.empty() ? nullptr : vkSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
    pipelineLayoutInfo.pPushConstantRanges = pushConstants.empty() ? nullptr : pushConstants.data();

    const auto createPipelineLayout = resolveCreatePipelineLayoutProc(m_device);
    VX_VK_CHECK_MSG(
        createPipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_handle),
        "setLayouts=" + std::to_string(vkSetLayouts.size()) + ", pushConstants=" + std::to_string(pushConstants.size()));

    ELIX_VK_CREATE_GUARD_DONE()
}

void PipelineLayout::destroyVkImpl()
{
    if (m_handle)
    {
        if (auto *destroyPipelineLayout = resolveDestroyPipelineLayoutProc(m_device))
            destroyPipelineLayout(m_device, m_handle, nullptr);
        m_handle = VK_NULL_HANDLE;
    }
}

PipelineLayout::~PipelineLayout()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END
