#ifndef ELIX_RENDER_PASS_HPP
#define ELIX_RENDER_PASS_HPP

#include "Core/Macros.hpp"

#include <memory>
#include <vector>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class RenderPass
{
public:
    using SharedPtr = std::shared_ptr<RenderPass>;
    
    RenderPass(VkDevice device, const std::vector<VkAttachmentDescription>& attachments, const std::vector<VkSubpassDescription>& subpasses, 
    const std::vector<VkSubpassDependency>& dependencies);

    static SharedPtr create(VkDevice device, const std::vector<VkAttachmentDescription>& attachments, const std::vector<VkSubpassDescription>& subpasses, 
    const std::vector<VkSubpassDependency>& dependencies);

    VkRenderPass vk();

    ~RenderPass();
private:
    VkDevice m_device{VK_NULL_HANDLE};
    VkRenderPass m_renderPass{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END


#endif //ELIX_RENDER_PASS_HPP