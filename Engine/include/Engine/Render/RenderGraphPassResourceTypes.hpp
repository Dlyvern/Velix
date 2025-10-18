#ifndef ELIX_RENDER_GRAPH_PASS_RESOURCE_TYPES_HPP
#define ELIX_RENDER_GRAPH_PASS_RESOURCE_TYPES_HPP

#include "Core/Macros.hpp"
#include "Core/RenderPass.hpp"

#include <functional>
#include <cstdint>
#include <string>
#include <cstddef>

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

        FormatSource source = FormatSource::Explicit;

        VkImageLayout targetLayout{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkPipelineStageFlags srcStage{VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};    
        VkPipelineStageFlags dstStage{VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
        VkAccessFlags srcAccess{0};
        VkAccessFlags dstAccess{VK_ACCESS_SHADER_READ_BIT};
    };

    struct RenderPassDescription
    {
        struct ColorAttachment
        {
            VkAttachmentReference colorAttachmentReference;
        };

        std::string name;
        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkSubpassDescription> subpassDescriptions;
        std::vector<VkSubpassDependency> subpassDependencies;
        std::vector<ColorAttachment> colorAttachments;
        std::vector<VkAttachmentReference> depthAttachments;
    };

    struct FramebufferDescription
    {
        std::string name;
        std::vector<std::size_t> attachmentsHash;
        core::RenderPass::SharedPtr renderPass; //TODO store render pass hash later
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