#include "Engine/Builders/RenderPassBuilder.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(builders)

RenderPassBuilder RenderPassBuilder::begin()
{
    return RenderPassBuilder{};
}

core::RenderPass::SharedPtr RenderPassBuilder::build()
{
    buildReferences();

    VkSubpassDescription des{};
    des.colorAttachmentCount = static_cast<uint32_t>(m_colorAttachmentReferences.size());
    des.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    des.pColorAttachments = m_colorAttachmentReferences.data();
    des.pDepthStencilAttachment = m_depthAttachments.empty() ? nullptr : &m_depthAttachmentReference;
    des.flags = 0;

    std::vector<VkAttachmentDescription> allAttachments;
    allAttachments.reserve(m_colorAttachments.size() + m_depthAttachments.size());
    allAttachments.insert(allAttachments.end(), m_colorAttachments.begin(), m_colorAttachments.end());
    allAttachments.insert(allAttachments.end(), m_depthAttachments.begin(), m_depthAttachments.end());

    return core::RenderPass::createShared(
        allAttachments,
        std::vector<VkSubpassDescription>{des},
        m_subpassDependencies);
}

void RenderPassBuilder::buildReferences()
{
    m_colorAttachmentReferences.clear();

    for (uint32_t index = 0; index < static_cast<uint32_t>(m_colorAttachments.size()); ++index)
    {
        VkAttachmentReference colorReference{};
        colorReference.attachment = index;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        m_colorAttachmentReferences.push_back(colorReference);
    }

    // Build depth attachment reference if it exists
    if (!m_depthAttachments.empty())
    {
        m_depthAttachmentReference.attachment = static_cast<uint32_t>(m_colorAttachments.size());
        m_depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    else
        m_depthAttachmentReference = VkAttachmentReference{};
}

RenderPassBuilder &RenderPassBuilder::addSubpassDependency(VkDependencyFlags dependencyFlags)
{
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcAccessMask = 0;
    dependency.dependencyFlags = dependencyFlags;

    if (!m_colorAttachments.empty())
    {
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    else if (!m_depthAttachments.empty())
    {
        dependency.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    else
    {
        dependency.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    m_subpassDependencies.push_back(dependency);

    return *this;
}

RenderPassBuilder &RenderPassBuilder::addDepthAttachment(VkFormat format)
{
    addDepthAttachment(format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
                       VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                       VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    return *this;
}

RenderPassBuilder &RenderPassBuilder::addDepthAttachment(
    VkFormat format, VkSampleCountFlagBits samples,
    VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp,
    VkAttachmentLoadOp stencilLoadOp, VkAttachmentStoreOp stencilStoreOp,
    VkImageLayout initialLayout, VkImageLayout finalLayout)
{
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = format;
    depthAttachment.samples = samples;
    depthAttachment.loadOp = loadOp;
    depthAttachment.storeOp = storeOp;
    depthAttachment.stencilLoadOp = stencilLoadOp;
    depthAttachment.stencilStoreOp = stencilStoreOp;
    depthAttachment.initialLayout = initialLayout;
    depthAttachment.finalLayout = finalLayout;
    m_depthAttachments.push_back(depthAttachment);
    return *this;
}

RenderPassBuilder &RenderPassBuilder::addColorAttachment(VkFormat format)
{
    addColorAttachment(format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
                       VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                       VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return *this;
}

RenderPassBuilder &RenderPassBuilder::addColorAttachment(
    VkFormat format, VkSampleCountFlagBits samples,
    VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp,
    VkAttachmentLoadOp stencilLoadOp, VkAttachmentStoreOp stencilStoreOp,
    VkImageLayout initialLayout, VkImageLayout finalLayout)
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = format;
    colorAttachment.samples = samples;
    colorAttachment.loadOp = loadOp;
    colorAttachment.storeOp = storeOp;
    colorAttachment.stencilLoadOp = stencilLoadOp;
    colorAttachment.stencilStoreOp = stencilStoreOp;
    colorAttachment.initialLayout = initialLayout;
    colorAttachment.finalLayout = finalLayout;
    m_colorAttachments.push_back(colorAttachment);
    return *this;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
