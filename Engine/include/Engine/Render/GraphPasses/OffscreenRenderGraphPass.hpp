#ifndef ELIX_OFFSCREEN_RENDER_GRAPH_PASS_HPP
#define ELIX_OFFSCREEN_RENDER_GRAPH_PASS_HPP

#include "Core/Macros.hpp"
#include "Engine/Render/RenderTarget.hpp"
#include "Core/Buffer.hpp"
#include "Engine/Skybox.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include <array>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class OffscreenRenderGraphPass : public IRenderGraphPass
{
public:
    OffscreenRenderGraphPass(VkDescriptorPool descriptorPool, uint32_t shadowId, RGPResourceHandler &shadowTextureHandler);
    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;
    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    std::vector<RGPResourceHandler> &getColorTextureHandlers()
    {
        return m_colorTextureHandler;
    }

    RGPResourceHandler &getObjectTextureHandler()
    {
        return m_objectIdTextureHandler;
    }

private:
    std::array<VkClearValue, 3> m_clearValues;

    std::vector<const RenderTarget *> m_colorRenderTargets;
    const RenderTarget *m_depthRenderTarget{nullptr};
    const RenderTarget *m_objectIdRenderTarget{nullptr};

    std::vector<VkFormat> m_colorFormats;
    VkFormat m_depthFormat;

    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};

    std::unique_ptr<Skybox> m_skybox{nullptr};

    VkExtent2D m_extent;
    VkViewport m_viewport;
    VkRect2D m_scissor;

    std::vector<RGPResourceHandler> m_colorTextureHandler;

    RGPResourceHandler m_depthTextureHandler;
    RGPResourceHandler m_objectIdTextureHandler;
    RGPResourceHandler &m_shadowTextureHandler;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_OFFSCREEN_RENDER_GRAPH_PASS_HPP