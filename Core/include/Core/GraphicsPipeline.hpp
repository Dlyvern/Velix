#ifndef ELIX_GRAPHICS_PIPELINE_HPP
#define ELIX_GRAPHICS_PIPELINE_HPP

#include "Core/Macros.hpp"
#include "Core/PipelineLayout.hpp"

#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class GraphicsPipeline
{
    DECLARE_VK_HANDLE_METHODS(VkPipeline)
    DECLARE_VK_SMART_PTRS(GraphicsPipeline, VkPipeline)
    ELIX_DECLARE_VK_LIFECYCLE()
public:
    GraphicsPipeline(VkDevice device, VkRenderPass renderPass, const std::vector<VkPipelineShaderStageCreateInfo> &shaderStages, VkPipelineLayout pipelineLayout,
                     VkPipelineDynamicStateCreateInfo dynamicState, VkPipelineColorBlendStateCreateInfo colorBlending, VkPipelineMultisampleStateCreateInfo multisampling,
                     VkPipelineRasterizationStateCreateInfo rasterizer, VkPipelineViewportStateCreateInfo viewportState, VkPipelineInputAssemblyStateCreateInfo inputAssembly,
                     VkPipelineVertexInputStateCreateInfo vertexInputInfo, uint32_t subpass, VkPipelineDepthStencilStateCreateInfo depthStencil, VkPipelineCache pipelineCache = VK_NULL_HANDLE);

    GraphicsPipeline(VkPipelineRenderingCreateInfo pipelineRendering, const std::vector<VkPipelineShaderStageCreateInfo> &shaderStages, VkPipelineLayout pipelineLayout,
                     VkPipelineDynamicStateCreateInfo dynamicState, VkPipelineColorBlendStateCreateInfo colorBlending, VkPipelineMultisampleStateCreateInfo multisampling,
                     VkPipelineRasterizationStateCreateInfo rasterizer, VkPipelineViewportStateCreateInfo viewportState, VkPipelineInputAssemblyStateCreateInfo inputAssembly,
                     VkPipelineVertexInputStateCreateInfo vertexInputInfo, VkPipelineDepthStencilStateCreateInfo depthStencil, uint32_t subpass = 0, VkPipelineCache pipelineCache = VK_NULL_HANDLE);

    void createVk(VkDevice device, VkRenderPass renderPass, const std::vector<VkPipelineShaderStageCreateInfo> &shaderStages, VkPipelineLayout pipelineLayout,
                  VkPipelineDynamicStateCreateInfo dynamicState, VkPipelineColorBlendStateCreateInfo colorBlending, VkPipelineMultisampleStateCreateInfo multisampling,
                  VkPipelineRasterizationStateCreateInfo rasterizer, VkPipelineViewportStateCreateInfo viewportState, VkPipelineInputAssemblyStateCreateInfo inputAssembly,
                  VkPipelineVertexInputStateCreateInfo vertexInputInfo, uint32_t subpass, VkPipelineDepthStencilStateCreateInfo depthStencil, VkPipelineCache pipelineCache = VK_NULL_HANDLE);

    void createVk(VkPipelineRenderingCreateInfo pipelineRendering, const std::vector<VkPipelineShaderStageCreateInfo> &shaderStages, VkPipelineLayout pipelineLayout,
                  VkPipelineDynamicStateCreateInfo dynamicState, VkPipelineColorBlendStateCreateInfo colorBlending, VkPipelineMultisampleStateCreateInfo multisampling,
                  VkPipelineRasterizationStateCreateInfo rasterizer, VkPipelineViewportStateCreateInfo viewportState, VkPipelineInputAssemblyStateCreateInfo inputAssembly,
                  VkPipelineVertexInputStateCreateInfo vertexInputInfo, VkPipelineDepthStencilStateCreateInfo depthStencil, uint32_t subpass = 0, VkPipelineCache pipelineCache = VK_NULL_HANDLE);

    ~GraphicsPipeline();

private:
    VkDevice m_device{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_GRAPHICS_PIPELINE_HPP