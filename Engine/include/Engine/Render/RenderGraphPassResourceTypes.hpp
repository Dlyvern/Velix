#ifndef ELIX_RENDER_GRAPH_PASS_RESOURCE_TYPES_HPP
#define ELIX_RENDER_GRAPH_PASS_RESOURCE_TYPES_HPP

#include "Core/Macros.hpp"
#include "Core/RenderPass.hpp"

#include <functional>
#include <cstdint>
#include <string>
#include <cstddef>

#include "Core/Shader.hpp"

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace RenderGraphPassResourceTypes
{
    enum class SizeClass 
    {
        Absolute,           // Fixed size in pixels
        SwapchainRelative,  // Matches swapchain dimensions
        Scaled,             // Swapchain dimensions * scale factor TODO add later
        Custom              // Custom calculation TODO add later
    };

    struct SizeSpec 
    {
        SizeClass type = SizeClass::Absolute;
        uint32_t width = 0;
        uint32_t height = 0;
        float scale = 1.0f;
        std::function<VkExtent2D()> customCalculator{nullptr};
    };

    struct TextureDescription
    {
        enum class FormatSource
        {
            Explicit,
            Swapchain, // Use swapchain format
        };

        std::string name;
        VkFormat format{VK_FORMAT_UNDEFINED};
        SizeSpec size{};
        VkImageUsageFlags usage{0};
        VkImageLayout initialLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT}; //TODO later for render pass
        VkImageAspectFlags aspect;
        uint32_t arrayLayers{1};
        VkMemoryPropertyFlags properties;
        VkImageTiling tiling;

        VkSampler sampler{VK_NULL_HANDLE};

        FormatSource source = FormatSource::Explicit;

        VkImageLayout targetLayout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkPipelineStageFlags srcStage{VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};    
        VkPipelineStageFlags dstStage{VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
        VkAccessFlags srcAccess{0};
        VkAccessFlags dstAccess{VK_ACCESS_SHADER_READ_BIT};
    };

    //TODO: Use TextureDescrption for attachments
    struct RenderPassDescription
    {
        struct ColorAttachment
        {
            VkAttachmentReference colorAttachmentReference;
        };

        struct SubpassDescription
        {
            // VkSubpassDescriptionFlags       flags;
            VkPipelineBindPoint pipelineBindPoint{VK_PIPELINE_BIND_POINT_GRAPHICS};
            // uint32_t                        inputAttachmentCount;
            // const VkAttachmentReference*    pInputAttachments;
            uint32_t colorAttachmentCount;
            std::vector<VkAttachmentReference> colorAttachments;
            // const VkAttachmentReference*    pResolveAttachments;
            std::vector<VkAttachmentReference> depthStencilAttachments;
            // uint32_t                        preserveAttachmentCount;
            // const uint32_t*                 pPreserveAttachments;
        };

        std::string name;
        std::vector<VkAttachmentDescription> attachments;
        std::vector<SubpassDescription> subpassDescriptions;
        std::vector<VkSubpassDependency> subpassDependencies;
    };

    struct GraphicsPipelineDescription
    {
        std::string name;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE
        };

        VkPipelineRasterizationStateCreateInfo rasterizer
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f
        };

        VkPipelineMultisampleStateCreateInfo multisampling
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE
        };

        VkPipelineDepthStencilStateCreateInfo depthStencil
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = {},
            .back = {},
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
        };

        VkPipelineColorBlendStateCreateInfo colorBlending
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .attachmentCount = 0,
            .pAttachments = nullptr,
        };

        VkPipelineViewportStateCreateInfo viewportState
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1
        };

        //TODO Fix it: std::vector<VkViewport> && std::vector<VkRect2D>
        VkViewport viewport;
        VkRect2D scissor;

        VkPipelineDynamicStateCreateInfo dynamicState
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
        };

        core::Shader::SharedPtr shader{nullptr};

        std::vector<VkDynamicState> dynamicStates;

        std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions;
        std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;

        std::vector<VkPipelineColorBlendAttachmentState> colorBlendingAttachments;

        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        std::size_t renderPassHash;
        uint32_t subpass = 0;
    };  

    struct FramebufferDescription
    {
        std::string name;
        std::vector<std::size_t> attachmentsHash;
        core::RenderPass::SharedPtr renderPass{nullptr}; //TODO store render pass hash later
        std::size_t renderPassHash;
        SizeSpec size;
        uint32_t layers;
    };

    struct BufferDescription
    {
        std::string name;
        VkDeviceSize size{0};
        VkBufferUsageFlags usage{0};
        VkMemoryPropertyFlags memoryProperties{VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};
    };

} //RenderGraphPassResourceTypes

ELIX_NESTED_NAMESPACE_END


#endif //ELIX_RENDER_GRAPH_PASS_RESOURCE_TYPES_HPP