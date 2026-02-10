#ifndef ELIX_RENDER_PASS_BUILDER_HPP
#define ELIX_RENDER_PASS_BUILDER_HPP

#include "Core/Macros.hpp"
#include "Core/RenderPass.hpp"

#include <vector>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(builders)

class RenderPassBuilder
{
public:
    static RenderPassBuilder begin();

    RenderPassBuilder &addDepthAttachment(VkFormat format);
    RenderPassBuilder &addDepthAttachment(VkFormat format, VkSampleCountFlagBits samples,
                                          VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp,
                                          VkAttachmentLoadOp stencilLoadOp, VkAttachmentStoreOp stencilStoreOp,
                                          VkImageLayout initialLayout, VkImageLayout finalLayout);

    RenderPassBuilder &addColorAttachment(VkFormat format);
    RenderPassBuilder &addColorAttachment(
        VkFormat format, VkSampleCountFlagBits samples,
        VkAttachmentLoadOp loadOp, VkAttachmentStoreOp storeOp,
        VkAttachmentLoadOp stencilLoadOp, VkAttachmentStoreOp stencilStoreOp,
        VkImageLayout initialLayout, VkImageLayout finalLayout);

    RenderPassBuilder &addSubpassDependency(VkDependencyFlags dependencyFlags = 0);

    core::RenderPass::SharedPtr build();

private:
    void buildReferences();

    std::vector<VkAttachmentDescription> m_colorAttachments;
    std::vector<VkAttachmentDescription> m_depthAttachments;

    std::vector<VkAttachmentReference> m_colorAttachmentReferences;
    VkAttachmentReference m_depthAttachmentReference;

    std::vector<VkSubpassDependency> m_subpassDependencies;
    std::vector<VkSubpassDescription> m_subpassDescriptions;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_PASS_BUILDER_HPP