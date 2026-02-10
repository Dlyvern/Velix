#ifndef ELIX_RENDER_PASS_HPP
#define ELIX_RENDER_PASS_HPP

#include "Core/Macros.hpp"

#include <vector>
#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class RenderPass
{
    DECLARE_VK_HANDLE_METHODS(VkRenderPass)
    DECLARE_VK_SMART_PTRS(RenderPass, VkRenderPass)
    ELIX_DECLARE_VK_LIFECYCLE()
public:
    RenderPass(const std::vector<VkAttachmentDescription> &attachments, const std::vector<VkSubpassDescription> &subpasses,
               const std::vector<VkSubpassDependency> &dependencies);

    void createVk(const std::vector<VkAttachmentDescription> &attachments, const std::vector<VkSubpassDescription> &subpasses,
                  const std::vector<VkSubpassDependency> &dependencies);

    ~RenderPass();
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_PASS_HPP