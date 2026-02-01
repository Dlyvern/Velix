#ifndef ELIX_IRENDER_GRAPH_PASS_HPP
#define ELIX_IRENDER_GRAPH_PASS_HPP

#include "Core/Macros.hpp"
#include "Core/CommandBuffer.hpp"

#include "Engine/Render/RenderGraphPassContext.hpp"
#include "Engine/Render/RenderGraphPassPerFrameData.hpp"

#include "Engine/Render/RenderGraph/RGPResourcesBuilder.hpp"
#include "Engine/Render/RenderGraph/RGPResourcesStorage.hpp"

#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class IRenderGraphPass
{
public:
    using SharedPtr = std::shared_ptr<IRenderGraphPass>;

    /// Called at the start of the graph setup phase.
    /// The pass should use this to declare which resources it requires
    /// but should NOT create GPU objects yet.
    virtual void setup(renderGraph::RGPResourcesBuilder &builder) = 0;

    /// Called after all proxies have been realized and GPU resources are created.
    virtual void compile(RGPResourcesStorage &storage) = 0;

    virtual void execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data) = 0;

    //! Leave this shit for now. Delete it later
    virtual void update(const RenderGraphPassContext &renderData) = 0;

    virtual void getRenderPassBeginInfo(VkRenderPassBeginInfo &renderPassBeginInfo) const = 0;

    virtual void beginRenderPass() {}
    virtual void endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer) {}

    virtual void cleanup() {}

    virtual void onSwapChainResized(renderGraph::RGPResourcesStorage &storage) {}

    virtual ~IRenderGraphPass() = default;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_IRENDER_GRAPH_PASS_HPP