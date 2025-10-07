#ifndef ELIX_IRENDER_GRAPH_PASS_HPP
#define ELIX_IRENDER_GRAPH_PASS_HPP

#include "Core/Macros.hpp"
#include "Core/CommandBuffer.hpp"

#include "Engine/Render/RenderGraphPassBuilder.hpp"

#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class IRenderGraphPass
{
public:
    using SharedPtr = std::shared_ptr<IRenderGraphPass>;

    /// Called at the start of the graph setup phase.
    /// The pass should use this to declare which resources it requires (via proxies)
    /// but should NOT create GPU objects yet.
    virtual void setup(std::shared_ptr<RenderGraphPassBuilder> builder) = 0;

    /// Called after all proxies have been realized and GPU resources are created.
    virtual void compile() = 0;
    virtual void execute(core::CommandBuffer::SharedPtr commandBuffer) = 0;

    //!Leave this shit for now. Delete it later
    virtual void update(uint32_t currentFrame, uint32_t currentImageIndex, VkFramebuffer fr) = 0;

    virtual void getRenderPassBeginInfo(VkRenderPassBeginInfo& renderPassBeginInfo) const = 0;

    virtual void cleanup() {}

    virtual ~IRenderGraphPass() = default;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_IRENDER_GRAPH_PASS_HPP