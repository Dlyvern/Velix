#ifndef ELIX_GRAPHICS_PIPELINE_BUILDER_HPP
#define ELIX_GRAPHICS_PIPELINE_BUILDER_HPP

#include "Core/Macros.hpp"
#include "Core/GraphicsPipeline.hpp"

#include <volk.h>

#include <vector>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class GraphicsPipelineBuilder
{
public:
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    VkPipelineMultisampleStateCreateInfo multisampling{};
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    VkPipelineViewportStateCreateInfo viewportState{};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    std::vector<VkDynamicState> dynamicStates;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    uint32_t subpass = 0;

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    GraphicsPipelineBuilder();
    
    std::shared_ptr<core::GraphicsPipeline> build(VkDevice device, const VkPipelineVertexInputStateCreateInfo& vertexInputInfo);
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_GRAPHICS_PIPELINE_BUILDER_HPP