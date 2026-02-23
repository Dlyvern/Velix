#include "Core/GraphicsPipeline.hpp"
#include "Core/VulkanContext.hpp"

#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

GraphicsPipeline::GraphicsPipeline(VkDevice device, VkRenderPass renderPass, const std::vector<VkPipelineShaderStageCreateInfo> &shaderStages,
                                   VkPipelineLayout pipelineLayout, VkPipelineDynamicStateCreateInfo dynamicState, VkPipelineColorBlendStateCreateInfo colorBlending, VkPipelineMultisampleStateCreateInfo multisampling,
                                   VkPipelineRasterizationStateCreateInfo rasterizer, VkPipelineViewportStateCreateInfo viewportState, VkPipelineInputAssemblyStateCreateInfo inputAssembly,
                                   VkPipelineVertexInputStateCreateInfo vertexInputInfo, uint32_t subpass, VkPipelineDepthStencilStateCreateInfo depthStencil, VkPipelineCache pipelineCache) : m_device(device)
{
    createVk(device, renderPass, shaderStages, pipelineLayout, dynamicState, colorBlending, multisampling, rasterizer, viewportState, inputAssembly, vertexInputInfo,
             subpass, depthStencil, pipelineCache);
}

GraphicsPipeline::GraphicsPipeline(VkPipelineRenderingCreateInfo pipelineRendering, const std::vector<VkPipelineShaderStageCreateInfo> &shaderStages, VkPipelineLayout pipelineLayout,
                                   VkPipelineDynamicStateCreateInfo dynamicState, VkPipelineColorBlendStateCreateInfo colorBlending, VkPipelineMultisampleStateCreateInfo multisampling,
                                   VkPipelineRasterizationStateCreateInfo rasterizer, VkPipelineViewportStateCreateInfo viewportState, VkPipelineInputAssemblyStateCreateInfo inputAssembly,
                                   VkPipelineVertexInputStateCreateInfo vertexInputInfo, VkPipelineDepthStencilStateCreateInfo depthStencil, uint32_t subpass, VkPipelineCache pipelineCache) : m_device(VulkanContext::getContext()->getDevice())
{
    createVk(pipelineRendering, shaderStages, pipelineLayout, dynamicState, colorBlending, multisampling, rasterizer, viewportState, inputAssembly, vertexInputInfo,
             depthStencil, subpass, pipelineCache);
}

void GraphicsPipeline::createVk(VkPipelineRenderingCreateInfo pipelineRendering, const std::vector<VkPipelineShaderStageCreateInfo> &shaderStages, VkPipelineLayout pipelineLayout,
                                VkPipelineDynamicStateCreateInfo dynamicState, VkPipelineColorBlendStateCreateInfo colorBlending, VkPipelineMultisampleStateCreateInfo multisampling,
                                VkPipelineRasterizationStateCreateInfo rasterizer, VkPipelineViewportStateCreateInfo viewportState, VkPipelineInputAssemblyStateCreateInfo inputAssembly,
                                VkPipelineVertexInputStateCreateInfo vertexInputInfo, VkPipelineDepthStencilStateCreateInfo depthStencil, uint32_t subpass, VkPipelineCache pipelineCache)
{
    ELIX_VK_CREATE_GUARD()

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext = &pipelineRendering;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = subpass;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;
    pipelineInfo.pDepthStencilState = &depthStencil;

    if (vkCreateGraphicsPipelines(m_device, pipelineCache, 1, &pipelineInfo, nullptr, &m_handle) != VK_SUCCESS)
        throw std::runtime_error("Failed to create graphics pipeline");

    ELIX_VK_CREATE_GUARD_DONE()
}

void GraphicsPipeline::createVk(VkDevice device, VkRenderPass renderPass, const std::vector<VkPipelineShaderStageCreateInfo> &shaderStages, VkPipelineLayout pipelineLayout,
                                VkPipelineDynamicStateCreateInfo dynamicState, VkPipelineColorBlendStateCreateInfo colorBlending, VkPipelineMultisampleStateCreateInfo multisampling,
                                VkPipelineRasterizationStateCreateInfo rasterizer, VkPipelineViewportStateCreateInfo viewportState, VkPipelineInputAssemblyStateCreateInfo inputAssembly,
                                VkPipelineVertexInputStateCreateInfo vertexInputInfo, uint32_t subpass, VkPipelineDepthStencilStateCreateInfo depthStencil, VkPipelineCache pipelineCache)
{
    ELIX_VK_CREATE_GUARD()

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
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

    if (vkCreateGraphicsPipelines(m_device, pipelineCache, 1, &pipelineInfo, nullptr, &m_handle) != VK_SUCCESS)
        throw std::runtime_error("Failed to create graphics pipeline");

    ELIX_VK_CREATE_GUARD_DONE()
}

void GraphicsPipeline::destroyVkImpl()
{
    if (m_handle)
    {
        vkDestroyPipeline(m_device, m_handle, nullptr);
        m_handle = VK_NULL_HANDLE;
    }
}

GraphicsPipeline::~GraphicsPipeline()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END