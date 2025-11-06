#include "Core/GraphicsPipeline.hpp"
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

GraphicsPipeline::GraphicsPipeline(VkDevice device, VkRenderPass renderPass, VkPipelineShaderStageCreateInfo* shaderStages, size_t stageCount,
VkPipelineLayout pipelineLayout, VkPipelineDynamicStateCreateInfo dynamicState, VkPipelineColorBlendStateCreateInfo colorBlending, VkPipelineMultisampleStateCreateInfo multisampling,
VkPipelineRasterizationStateCreateInfo rasterizer, VkPipelineViewportStateCreateInfo viewportState, VkPipelineInputAssemblyStateCreateInfo inputAssembly,
VkPipelineVertexInputStateCreateInfo vertexInputInfo, uint32_t subpass, VkPipelineDepthStencilStateCreateInfo depthStencil) :
m_device(device)
{   
    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = stageCount;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = subpass;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;
    pipelineInfo.pDepthStencilState = &depthStencil;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline) != VK_SUCCESS)
            throw std::runtime_error("Failed to create graphics pipeline");
}

VkPipeline GraphicsPipeline::vk()
{
    return m_graphicsPipeline;
}

GraphicsPipeline::~GraphicsPipeline()
{
    if(m_graphicsPipeline)
    {
        vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;        
    }
}

ELIX_NESTED_NAMESPACE_END