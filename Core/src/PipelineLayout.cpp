#include "Core/PipelineLayout.hpp"
#include "Core/VulkanAssert.hpp"
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(core)

PipelineLayout::PipelineLayout(VkDevice device, const std::vector<std::reference_wrapper<const DescriptorSetLayout>> &setLayouts, const std::vector<VkPushConstantRange> &pushConstants)
    : m_device(device)
{
    createVk(setLayouts, pushConstants);
}

void PipelineLayout::createVk(const std::vector<std::reference_wrapper<const DescriptorSetLayout>> &setLayouts, const std::vector<VkPushConstantRange> &pushConstants)
{
    ELIX_VK_CREATE_GUARD()

    //*That is kinda sad...

    std::vector<VkDescriptorSetLayout> vkSetLayouts;

    for (const auto &des : setLayouts)
        vkSetLayouts.push_back(des.get().vk());

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(vkSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = vkSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
    pipelineLayoutInfo.pPushConstantRanges = pushConstants.data();

    VX_VK_CHECK(vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_handle));

    ELIX_VK_CREATE_GUARD_DONE()
}

void PipelineLayout::destroyVkImpl()
{
    if (m_handle)
    {
        vkDestroyPipelineLayout(m_device, m_handle, nullptr);
        m_handle = VK_NULL_HANDLE;
    }
}

PipelineLayout::~PipelineLayout()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END
