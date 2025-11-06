#ifndef ELIX_GRAPHICS_PIPELINE_HPP
#define ELIX_GRAPHICS_PIPELINE_HPP

#include "Core/Macros.hpp"

#include <cstdint>
#include <memory>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class GraphicsPipeline
{
public:
    using SharedPtr = std::shared_ptr<GraphicsPipeline>;
    
    GraphicsPipeline(VkDevice device, VkRenderPass renderPass, VkPipelineShaderStageCreateInfo* shaderStages, size_t stageCount, VkPipelineLayout pipelineLayout,
    VkPipelineDynamicStateCreateInfo dynamicState, VkPipelineColorBlendStateCreateInfo colorBlending, VkPipelineMultisampleStateCreateInfo multisampling,
    VkPipelineRasterizationStateCreateInfo rasterizer, VkPipelineViewportStateCreateInfo viewportState, VkPipelineInputAssemblyStateCreateInfo inputAssembly,
    VkPipelineVertexInputStateCreateInfo vertexInputInfo, uint32_t subpass, VkPipelineDepthStencilStateCreateInfo depthStencil);
    ~GraphicsPipeline();

    VkPipeline vk();

private:
    VkPipeline m_graphicsPipeline{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_GRAPHICS_PIPELINE_HPP