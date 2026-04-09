#ifndef ELIX_EDITOR_BILLBOARD_RENDER_GRAPH_PASS_HPP
#define ELIX_EDITOR_BILLBOARD_RENDER_GRAPH_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Scene.hpp"
#include "Engine/Texture.hpp"

#include "Core/PipelineLayout.hpp"
#include "Core/DescriptorSetLayout.hpp"
#include "Core/Sampler.hpp"

#include <array>
#include <vector>
#include <memory>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

/// Editor-only pass that draws textured icon billboards (camera / light / audio)
/// at the world position of entities that carry those components.
/// Never included in a shipping/game build.
class EditorBillboardRenderGraphPass : public engine::renderGraph::IRenderGraphPass
{
public:
    EditorBillboardRenderGraphPass(std::shared_ptr<engine::Scene>               scene,
                                   std::vector<engine::renderGraph::RGPResourceHandler> &inputHandlers);

    void setup(engine::renderGraph::RGPResourcesBuilder &builder) override;
    void compile(engine::renderGraph::RGPResourcesStorage &storage) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer,
                const engine::RenderGraphPassPerFrameData &data,
                const engine::RenderGraphPassContext &renderContext) override;
    void freeResources() override;

    std::vector<RenderPassExecution> getRenderPassExecutions(
        const engine::RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);
    // Sets the same icon image for camera/light/audio.
    void setIconTexturePath(std::string texturePath);
    void setCameraIconTexturePath(std::string texturePath);
    void setLightIconTexturePath(std::string texturePath);
    void setAudioIconTexturePath(std::string texturePath);
    void setScene(std::shared_ptr<engine::Scene> scene);
    void setBillboardsVisible(bool visible);
    std::vector<engine::renderGraph::RGPResourceHandler> &getHandlers() { return m_outputHandlers; }

private:
    struct EditorBillboardPC
    {
        glm::mat4 viewProj;   // 64
        glm::vec3 right;      // 12
        float     size;       // 4  -> 80
        glm::vec3 up;         // 12
        float     pad0;       // 4  -> 96
        glm::vec3 worldPos;   // 12
        int       pad1;       // 4  -> 112
        glm::vec4 color;      // 16 -> 128
    };

    std::shared_ptr<engine::Scene> m_scene;

    std::vector<engine::renderGraph::RGPResourceHandler> &m_inputHandlers;
    std::vector<engine::renderGraph::RGPResourceHandler>  m_outputHandlers;

    std::vector<const engine::RenderTarget *> m_outputRenderTargets;

    core::DescriptorSetLayout::SharedPtr m_textureDescriptorSetLayout{nullptr};
    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::Sampler::SharedPtr m_sampler{nullptr};
    std::array<engine::Texture::SharedPtr, 3> m_iconTextures{};
    std::array<VkDescriptorSet, 3> m_iconDescriptorSets{VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> m_passthroughDescriptorSets;
    bool m_passthroughSetsBuilt{false};
    bool m_showBillboards{true};

    VkFormat   m_format{VK_FORMAT_UNDEFINED};
    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D   m_scissor{};

    std::array<VkClearValue, 1> m_clearValues{};

    std::array<std::string, 3> m_iconTexturePaths{
        "", // camera: generated icon by default
        "", // light: generated icon by default
        ""  // audio: generated icon by default
    };
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_BILLBOARD_RENDER_GRAPH_PASS_HPP
