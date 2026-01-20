#include "Core/PipelineLayout.hpp"
#include <cstdint>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

PipelineLayout::PipelineLayout(VkDevice device, const std::vector<DescriptorSetLayout::SharedPtr> &setLayouts, const std::vector<VkPushConstantRange> &pushConstants)
    : m_device(device)
{
    createVk(setLayouts, pushConstants);
}

void PipelineLayout::createVk(const std::vector<DescriptorSetLayout::SharedPtr> &setLayouts, const std::vector<VkPushConstantRange> &pushConstants)
{
    ELIX_VK_CREATE_GUARD()

    //*That is kinda sad...

    std::vector<VkDescriptorSetLayout> vkSetLayouts;

    for (auto &des : setLayouts)
        vkSetLayouts.push_back(des->vk());

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(vkSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = vkSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
    pipelineLayoutInfo.pPushConstantRanges = pushConstants.data();

    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_handle) != VK_SUCCESS)
        throw std::runtime_error("Failed to create pipeline layout");

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