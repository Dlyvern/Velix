#ifndef ELIX_GRAPHICS_PIPELINE_BUILDER_HPP
#define ELIX_GRAPHICS_PIPELINE_BUILDER_HPP

#include "Core/Macros.hpp"
#include "Core/GraphicsPipeline.hpp"

#include <vector>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(builders)

// TODO add PSO here
class GraphicsPipelineBuilder
{
public:
    static VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI(VkPrimitiveTopology topology)
    {
        VkPipelineInputAssemblyStateCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ci.topology = topology;
        ci.primitiveRestartEnable = VK_FALSE;
        return ci;
    }

    static VkPipelineRasterizationStateCreateInfo rasterizationCI(VkPolygonMode polygonMode)
    {
        VkPipelineRasterizationStateCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        ci.depthClampEnable = VK_FALSE;
        ci.rasterizerDiscardEnable = VK_FALSE;
        ci.polygonMode = polygonMode;
        ci.lineWidth = 1.0f;
        ci.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        ci.depthBiasEnable = VK_FALSE;
        ci.depthBiasConstantFactor = 0.0f;
        ci.depthBiasSlopeFactor = 0.0f;
        ci.depthBiasClamp = 0.0f;
        ci.cullMode = VK_CULL_MODE_BACK_BIT;

        return ci;
    }

    static VkPipelineMultisampleStateCreateInfo multisamplingCI()
    {
        VkPipelineMultisampleStateCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ci.sampleShadingEnable = VK_FALSE;
        ci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        ci.minSampleShading = 1.0f;
        ci.pSampleMask = nullptr;
        ci.alphaToCoverageEnable = VK_FALSE;
        ci.alphaToOneEnable = VK_FALSE;
        return ci;
    }

    static VkPipelineDepthStencilStateCreateInfo depthStencilCI(bool depthTest, bool depthWrite, VkCompareOp compareOp)
    {
        VkPipelineDepthStencilStateCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ci.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
        ci.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        ci.depthCompareOp = depthTest ? compareOp : VK_COMPARE_OP_ALWAYS;
        ci.depthBoundsTestEnable = VK_FALSE;
        ci.stencilTestEnable = VK_FALSE;
        ci.minDepthBounds = 0.0f;
        ci.maxDepthBounds = 1.0f;
        return ci;
    }

    static VkPipelineColorBlendStateCreateInfo colorBlending(const std::vector<VkPipelineColorBlendAttachmentState> &blendingAttachments)
    {
        VkPipelineColorBlendStateCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        ci.blendConstants[0] = 0.0f;
        ci.blendConstants[1] = 0.0f;
        ci.blendConstants[2] = 0.0f;
        ci.blendConstants[3] = 0.0f;
        ci.logicOpEnable = VK_FALSE;
        ci.attachmentCount = static_cast<uint32_t>(blendingAttachments.size());
        ci.pAttachments = blendingAttachments.empty() ? nullptr : blendingAttachments.data();
        return ci;
    }

    static VkPipelineColorBlendAttachmentState colorBlendAttachmentCI(bool blendEnable = false, VkBlendFactor srcColor = VK_BLEND_FACTOR_SRC_ALPHA,
                                                                      VkBlendFactor dstColor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
    {
        VkPipelineColorBlendAttachmentState ci{};
        ci.blendEnable = VK_FALSE;
        ci.srcColorBlendFactor = srcColor;
        ci.dstColorBlendFactor = dstColor;
        ci.colorBlendOp = VK_BLEND_OP_ADD;
        ci.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        ci.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        ci.alphaBlendOp = VK_BLEND_OP_ADD;
        ci.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                            VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT |
                            VK_COLOR_COMPONENT_A_BIT;

        return ci;
    }

    static VkPipelineVertexInputStateCreateInfo vertexInputCI(const std::vector<VkVertexInputBindingDescription> &binding,
                                                              const std::vector<VkVertexInputAttributeDescription> &attribute)
    {
        VkPipelineVertexInputStateCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        ci.vertexBindingDescriptionCount = static_cast<uint32_t>(binding.size());
        ci.pVertexBindingDescriptions = binding.data();
        ci.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute.size());
        ci.pVertexAttributeDescriptions = attribute.data();
        return ci;
    }

    static VkPipelineViewportStateCreateInfo viewportCI(const std::vector<VkViewport> &viewports, const std::vector<VkRect2D> &scissor)
    {
        VkPipelineViewportStateCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        ci.viewportCount = static_cast<uint32_t>(viewports.size());
        ci.scissorCount = static_cast<uint32_t>(scissor.size());
        ci.pViewports = viewports.data();
        ci.pScissors = scissor.data();
        return ci;
    }

    static VkPipelineDynamicStateCreateInfo dynamic(const std::vector<VkDynamicState> &dynamicStates)
    {
        VkPipelineDynamicStateCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        ci.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        ci.pDynamicStates = dynamicStates.data();
        return ci;
    }
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_GRAPHICS_PIPELINE_BUILDER_HPP0