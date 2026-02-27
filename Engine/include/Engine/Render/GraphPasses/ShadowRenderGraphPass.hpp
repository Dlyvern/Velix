#ifndef ELIX_SHADOW_RENDER_GRAPH_PASS_HPP
#define ELIX_SHADOW_RENDER_GRAPH_PASS_HPP

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class ShadowRenderGraphPass : public IRenderGraphPass
{
public:
    ShadowRenderGraphPass();
    void record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;
    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setup(RGPResourcesBuilder &builder) override;
    void compile(RGPResourcesStorage &storage) override;
    void cleanup() override;

    RGPResourceHandler &getDirectionalShadowHandler()
    {
        return m_depthTextureHandler;
    }

    RGPResourceHandler &getSpotShadowHandler()
    {
        return m_depthArrayTextureHandler;
    }

    RGPResourceHandler &getCubeShadowHandler()
    {
        return m_depthCubeTextureHandler;
    }

private:
    enum class ShadowExecutionType : uint8_t
    {
        Directional,
        Spot,
        Point
    };

    struct ShadowExecutionInfo
    {
        ShadowExecutionType type{ShadowExecutionType::Directional};
        uint32_t layer{0};
        uint32_t lightIndex{0};
        uint32_t faceIndex{0};
    };

    void destroyLayerViews();
    void rebuildLayerViews();
    VkImageView createSingleLayerView(const RenderTarget *target, uint32_t baseArrayLayer) const;

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};

    const RenderTarget *m_renderTarget{nullptr};
    const RenderTarget *m_cubeRenderTarget{nullptr};
    const RenderTarget *m_arrayRenderTarget{nullptr};

    VkFormat m_depthFormat;

    VkViewport m_viewport{};
    VkRect2D m_scissor{};

    VkExtent2D m_extent{2096, 2096};

    VkClearValue m_clearValue;

    RGPResourceHandler m_depthTextureHandler;
    RGPResourceHandler m_depthCubeTextureHandler;
    RGPResourceHandler m_depthArrayTextureHandler;

    std::vector<VkImageView> m_spotLayerViews;
    std::vector<VkImageView> m_pointLayerViews;
    std::vector<VkImageView> m_directionalLayerViews;
    std::vector<ShadowExecutionInfo> m_executionInfos;
    mutable size_t m_currentExecutionIndex{0};
};
ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SHADOW_RENDER_GRAPH_PASS_HPP
