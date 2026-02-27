#ifndef ELIX_IRENDER_GRAPH_PASS_HPP
#define ELIX_IRENDER_GRAPH_PASS_HPP

#include "Core/Macros.hpp"
#include "Core/CommandBuffer.hpp"

#include "Engine/Render/RenderGraphPassPerFrameData.hpp"

#include "Engine/Render/RenderGraph/RGPResourcesBuilder.hpp"
#include "Engine/Render/RenderGraph/RGPResourcesStorage.hpp"

#include <memory>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class IRenderGraphPass
{
public:
    struct RenderPassExecution
    {
        VkRect2D renderArea;

        std::vector<VkRenderingAttachmentInfo> colorsRenderingItems;
        VkRenderingAttachmentInfo depthRenderingItem;
        std::vector<VkFormat> colorFormats;
        VkFormat depthFormat{VK_FORMAT_UNDEFINED};

        bool useDepth{true};

        std::unordered_map<RGPResourceHandler, const RenderTarget *> targets;

        RenderPassExecution()
        {
            renderArea.offset = {0, 0};
        }
    };

    using SharedPtr = std::shared_ptr<IRenderGraphPass>;

    /// Called at the start of the graph setup phase.
    /// The pass should use this to declare which resources it requires
    /// but should NOT create GPU objects yet.
    virtual void setup(renderGraph::RGPResourcesBuilder &builder) = 0;

    /// Called after all proxies have been realized and GPU resources are created.
    virtual void compile(RGPResourcesStorage &storage) = 0;

    virtual void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                        const RenderGraphPassContext &renderContext) = 0;

    virtual std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const = 0;

    virtual void cleanup() {}

    virtual ~IRenderGraphPass() = default;

    void setDebugName(const std::string &name)
    {
        m_debugName = name;
    }

    const std::string &getDebugName() const
    {
        return m_debugName;
    }

    void requestRecompilation()
    {
        m_needsRecompilation = true;
    }

    void recompilationIsDone()
    {
        m_needsRecompilation = false;
    }

    bool needsRecompilation()
    {
        return m_needsRecompilation;
    }

private:
    std::string m_debugName;
    bool m_needsRecompilation{false};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_IRENDER_GRAPH_PASS_HPP
