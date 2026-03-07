#ifndef ELIX_UI_RENDER_GRAPH_PASS_HPP
#define ELIX_UI_RENDER_GRAPH_PASS_HPP

#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"
#include "Core/Buffer.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/UI/FontAtlas.hpp"
#include "Engine/UI/UIRenderData.hpp"

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <array>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

/// Renders all in-scene UI game objects (Billboard, UIText, UIButton).
/// Must be inserted in the render graph AFTER the scene colour output is ready and
/// BEFORE the ImGui (or Present) pass.
///
/// The render graph orchestrator is responsible for collecting ui::UIRenderData
/// from the scene each frame and calling setRenderData() before record() runs.
class UIRenderGraphPass : public IRenderGraphPass
{
public:
    explicit UIRenderGraphPass(std::vector<RGPResourceHandler> &inputHandlers);

    void setup(RGPResourcesBuilder &builder) override;
    void compile(RGPResourcesStorage &storage) override;
    void prepareRecord(const RenderGraphPassPerFrameData &data,
                       const RenderGraphPassContext &renderContext) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer,
                const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(const RenderGraphPassContext &renderContext) const override;

    void setExtent(VkExtent2D extent);

    /// Called by the render graph orchestrator each frame before record().
    void setRenderData(const ui::UIRenderData &data);

    std::vector<RGPResourceHandler> &getHandlers() { return m_outputHandlers; }
    std::vector<VkImageView> getOutputImageViews() const;

private:
    struct BillboardPC
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

    struct UITextPC
    {
        glm::vec4 color;      // 16
    };

    struct UIQuadPC
    {
        glm::vec4 color;        // 16
        glm::vec4 borderColor;  // 32
        float     borderWidth;  // 36
        float     cornerRadius; // 40
        float     pad0;         // 44
        float     pad1;         // 48
    };

    void recordPassthrough(core::CommandBuffer::SharedPtr commandBuffer, uint32_t frameIndex);
    void recordBillboards(core::CommandBuffer::SharedPtr commandBuffer);
    void recordUIText(core::CommandBuffer::SharedPtr commandBuffer);
    void recordUIButtons(core::CommandBuffer::SharedPtr commandBuffer);

    ui::FontAtlas *getOrBuildAtlas(const ui::Font *font);
    core::Buffer::SharedPtr uploadVertices(const std::vector<vertex::Vertex2D> &verts, uint32_t currentFrame);
    VkDescriptorSet getTextureDescriptorSet(VkImageView view, VkSampler sampler);

    struct PreparedBillboardDraw
    {
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        BillboardPC pushConstants{};
    };

    struct PreparedTextDraw
    {
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        core::Buffer::SharedPtr vertexBuffer{nullptr};
        uint32_t vertexCount{0};
        UITextPC pushConstants{};
    };

    struct PreparedButtonDraw
    {
        core::Buffer::SharedPtr backgroundVertexBuffer{nullptr};
        uint32_t backgroundVertexCount{0};
        UIQuadPC backgroundPushConstants{};

        bool hasLabel{false};
        VkDescriptorSet labelDescriptorSet{VK_NULL_HANDLE};
        core::Buffer::SharedPtr labelVertexBuffer{nullptr};
        uint32_t labelVertexCount{0};
        UITextPC labelPushConstants{};
    };

    std::vector<RGPResourceHandler> &m_inputHandlers;
    std::vector<RGPResourceHandler>  m_outputHandlers;

    ui::UIRenderData m_renderData;

    VkFormat   m_format{VK_FORMAT_UNDEFINED};
    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D   m_scissor{};

    std::vector<const RenderTarget *> m_outputRenderTargets;

    core::DescriptorSetLayout::SharedPtr m_textureDescriptorSetLayout{nullptr};

    core::PipelineLayout::SharedPtr m_passthroughPipelineLayout{nullptr};
    core::PipelineLayout::SharedPtr m_billboardPipelineLayout{nullptr};
    core::PipelineLayout::SharedPtr m_textPipelineLayout{nullptr};
    core::PipelineLayout::SharedPtr m_quadPipelineLayout{nullptr};

    // Per-frame descriptor sets that bind the input (scene colour) texture for passthrough
    std::vector<VkDescriptorSet> m_passthroughDescriptorSets;
    bool                         m_passthroughSetsBuilt{false};

    core::Sampler::SharedPtr m_nearestSampler{nullptr};
    core::Sampler::SharedPtr m_linearSampler{nullptr};

    std::unordered_map<std::string, std::unique_ptr<ui::FontAtlas>> m_fontAtlases;
    std::unordered_map<VkImageView, VkDescriptorSet>                m_texDescriptorSets;
    std::vector<std::vector<core::Buffer::SharedPtr>>               m_transientVertexBuffersByFrame;
    std::vector<PreparedBillboardDraw>                              m_preparedBillboards;
    std::vector<PreparedTextDraw>                                   m_preparedTexts;
    std::vector<PreparedButtonDraw>                                 m_preparedButtons;

    std::array<VkClearValue, 1> m_clearValues{};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_UI_RENDER_GRAPH_PASS_HPP
