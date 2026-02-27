#ifndef ELIX_SKY_LIGHT_RENDER_GRAPH_PASS_HPP
#define ELIX_SKY_LIGHT_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/RenderTarget.hpp"
#include "Engine/SkyLightSystem.hpp"
#include "Engine/Skybox.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class SkyLightRenderGraphPass : public IRenderGraphPass
{
public:
    SkyLightRenderGraphPass(std::vector<RGPResourceHandler> &lightingInputHandlers,
                            RGPResourceHandler &depthTextureHandler);

    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    void compile(renderGraph::RGPResourcesStorage &storage) override;
    void setup(renderGraph::RGPResourcesBuilder &builder) override;

    std::vector<RGPResourceHandler> &getOutput()
    {
        return m_colorTextureHandler;
    }

private:
    std::vector<const RenderTarget *> m_colorRenderTargets;
    const RenderTarget *m_depthRenderTarget{nullptr};

    VkFormat m_colorFormat{VK_FORMAT_UNDEFINED};
    VkFormat m_depthFormat{VK_FORMAT_UNDEFINED};

    std::vector<RGPResourceHandler> &m_lightingInputHandlers;
    RGPResourceHandler &m_depthTextureHandler;
    std::vector<RGPResourceHandler> m_colorTextureHandler;

    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D m_scissor{};

    std::unique_ptr<SkyLightSystem> m_skyLightSystem{nullptr};
    std::unique_ptr<Skybox> m_skybox{nullptr};
    std::string m_loadedSkyboxHDRPath;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SKY_LIGHT_RENDER_GRAPH_PASS_HPP
