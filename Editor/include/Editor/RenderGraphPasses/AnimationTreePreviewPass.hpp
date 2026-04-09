#ifndef ELIX_ANIMATION_TREE_PREVIEW_PASS_HPP
#define ELIX_ANIMATION_TREE_PREVIEW_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Core/Buffer.hpp"
#include "Core/DescriptorSetLayout.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include <glm/glm.hpp>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

struct AnimPreviewDrawData
{
    std::vector<engine::GPUMesh *> meshes;
    std::vector<engine::Material *> materials;
    std::vector<glm::mat4> meshModelMatrices;
    glm::mat4 modelMatrix{1.0f};
    glm::mat4 viewMatrix{1.0f};
    glm::mat4 projMatrix{1.0f};
    bool hasData{false};
};

class AnimationTreePreviewPass : public engine::renderGraph::IRenderGraphPass
{
public:
    static constexpr uint32_t PREVIEW_SIZE = 384;

    AnimationTreePreviewPass();

    void setup(engine::renderGraph::RGPResourcesBuilder &builder) override;
    void compile(engine::renderGraph::RGPResourcesStorage &storage) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer,
                const engine::RenderGraphPassPerFrameData &data,
                const engine::RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(
        const engine::RenderGraphPassContext &renderContext) const override;

    void setPreviewData(const AnimPreviewDrawData &data)
    {
        m_pendingData = data;
    }

    VkImageView getOutputImageView() const;
    VkSampler getOutputSampler() const;

    void freeResources() override;

private:
    engine::renderGraph::RGPResourceHandler m_colorHandler{};
    engine::renderGraph::RGPResourceHandler m_depthHandler{};

    const engine::RenderTarget *m_colorTarget{nullptr};
    const engine::RenderTarget *m_depthTarget{nullptr};
    VkFormat m_depthFormat{VK_FORMAT_UNDEFINED};

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    core::Sampler::SharedPtr m_sampler{nullptr};

    // Per-frame camera UBOs so the panel can supply its own orbit view/proj.
    std::vector<core::Buffer::SharedPtr>          m_cameraUBOs;
    std::vector<VkDescriptorSet>                  m_cameraDescriptorSets;
    core::DescriptorSetLayout::SharedPtr          m_cameraDescriptorSetLayout;
    bool                                          m_cameraDescriptorSetsInitialized{false};

    VkViewport m_viewport{};
    VkRect2D m_scissor{};
    std::array<VkClearValue, 2> m_clearValues{};

    AnimPreviewDrawData m_pendingData{};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ANIMATION_TREE_PREVIEW_PASS_HPP
