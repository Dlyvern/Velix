#include "Core/PipelineLayout.hpp"
#include <cstdint>
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

PipelineLayout::PipelineLayout(VkDevice device, const std::vector<DescriptorSetLayout::SharedPtr>& setLayouts, const std::vector<VkPushConstantRange>& pushConstants)
: m_device(device)
{
    //*That is kinda sad...
    std::vector<VkDescriptorSetLayout> vkSetLayouts;

    for(auto& des : setLayouts)
        vkSetLayouts.push_back(des->vk());

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(vkSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = vkSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
    pipelineLayoutInfo.pPushConstantRanges = pushConstants.data();

    if(vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create pipeline layout");
}

PipelineLayout::SharedPtr PipelineLayout::create(VkDevice device, const std::vector<DescriptorSetLayout::SharedPtr>& setLayouts, const std::vector<VkPushConstantRange>& pushConstants)
{
    return std::make_shared<PipelineLayout>(device, setLayouts, pushConstants);
}

VkPipelineLayout PipelineLayout::vk()
{
    return m_pipelineLayout;
}

PipelineLayout::~PipelineLayout()
{
    if(m_pipelineLayout)
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
}

ELIX_NESTED_NAMESPACE_END