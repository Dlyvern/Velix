#ifndef ELIX_RENDER_PASS_RENDER_GRAPH_PROXY_HPP
#define ELIX_RENDER_PASS_RENDER_GRAPH_PROXY_HPP

#include "Core/Macros.hpp"
#include "Core/RenderPass.hpp"

#include "Engine/Render/Proxies/IRenderGraphProxy.hpp"

#include <memory>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class RenderPassRenderGraphProxy : public IRenderGraphProxy<core::RenderPass, RenderGraphProxySinglePtrData, core::RenderPass>
{
public:
    using SharedPtr = std::shared_ptr<RenderPassRenderGraphProxy>;

    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkSubpassDescription> subpasses;
    std::vector<VkSubpassDependency> dependencies;

    explicit RenderPassRenderGraphProxy(const std::string& name) : IRenderGraphProxy(name) {}
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_RENDER_PASS_RENDER_GRAPH_PROXY_HPP