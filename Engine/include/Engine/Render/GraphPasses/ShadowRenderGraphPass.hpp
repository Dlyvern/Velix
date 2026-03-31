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
    void syncQualitySettings();
    void syncActiveShadowCounts(uint32_t activeSpotCount, uint32_t activePointCount);

    // When true, record() skips all draw calls and the shadow maps are cleared to
    // depth=1.0 (no shadow). Use when RT shadows fully replace raster shadows.
    void setSkipRendering(bool skip) { m_skipRendering = skip; }

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

    struct Outputs
    {
        RGPOutputSlot<SingleHandle> directionalShadow;
        RGPOutputSlot<SingleHandle> spotShadow;
        RGPOutputSlot<SingleHandle> cubeShadow;
    } outputs;

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
    void setShadowExtent(VkExtent2D extent);
    void updateShadowExtentFromSettings();
    void updateDirectionalCascadeCountFromSettings();

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};

    const RenderTarget *m_renderTarget{nullptr};
    const RenderTarget *m_cubeRenderTarget{nullptr};
    const RenderTarget *m_arrayRenderTarget{nullptr};

    VkFormat m_depthFormat;

    VkViewport m_viewport{};
    VkRect2D m_scissor{};

    VkExtent2D m_extent{2048, 2048};
    uint32_t m_directionalCascadeCount{ShadowConstants::MAX_DIRECTIONAL_CASCADES};
    uint32_t m_spotShadowCount{ShadowConstants::MAX_SPOT_SHADOWS};
    uint32_t m_pointShadowCount{ShadowConstants::MAX_POINT_SHADOWS};

    VkClearValue m_clearValue;
    bool m_skipRendering{false};

    RGPResourceHandler m_depthTextureHandler;
    RGPResourceHandler m_depthCubeTextureHandler;
    RGPResourceHandler m_depthArrayTextureHandler;

    std::vector<VkImageView> m_spotLayerViews;
    std::vector<VkImageView> m_pointLayerViews;
    std::vector<VkImageView> m_directionalLayerViews;
    std::vector<ShadowExecutionInfo> m_executionInfos;
};
ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SHADOW_RENDER_GRAPH_PASS_HPP
