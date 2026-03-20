#ifndef ELIX_IRENDER_GRAPH_PASS_HPP
#define ELIX_IRENDER_GRAPH_PASS_HPP

#include "Core/Macros.hpp"
#include "Core/CommandBuffer.hpp"

#include "Engine/Render/RenderGraphPassPerFrameData.hpp"

#include "Engine/Render/RenderGraph/RGPResourcesBuilder.hpp"
#include "Engine/Render/RenderGraph/RGPResourcesStorage.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class IRenderGraphPass
{
public:
    enum class ExecutionMode : uint8_t
    {
        DynamicRendering,
        Direct
    };

    struct RenderPassExecution
    {
        VkRect2D renderArea;

        std::vector<VkRenderingAttachmentInfo> colorsRenderingItems;
        VkRenderingAttachmentInfo depthRenderingItem;
        std::vector<VkFormat> colorFormats;
        VkFormat depthFormat{VK_FORMAT_UNDEFINED};
        VkSampleCountFlagBits rasterizationSamples{VK_SAMPLE_COUNT_1_BIT};

        bool useDepth{true};
        ExecutionMode mode{ExecutionMode::DynamicRendering};

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

    virtual void prepareRecord(const RenderGraphPassPerFrameData &, const RenderGraphPassContext &) {}

    virtual void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                        const RenderGraphPassContext &renderContext) = 0;

    virtual uint64_t getExecutionCacheKey(const RenderGraphPassContext &) const
    {
        return 0u;
    }

    /// Dynamic feature toggle for the pass implementation.
    /// RenderGraph keeps the pass in the execution chain; passes with downstream
    /// consumers should handle the disabled state internally (typically as a
    /// passthrough/no-op) instead of relying on graph-level skipping.
    virtual bool isEnabled() const { return true; }

    /// Return false for passes that touch thread-unsafe global state during command recording.
    virtual bool canRecordInParallel() const { return true; }

    virtual std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const = 0;

    virtual void cleanup() {}

    /// Free all GPU resources owned by this pass (VkImages, descriptor sets, etc.).
    /// Called by RenderGraph::disablePass<T>() before removing the pass from the draw loop.
    /// Does NOT destroy pipelines — those are format/layout dependent, not image dependent.
    /// Default no-op so all existing passes compile without changes.
    virtual void freeResources() {}

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
