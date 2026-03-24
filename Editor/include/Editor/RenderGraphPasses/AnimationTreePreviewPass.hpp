#ifndef ELIX_ANIMATION_TREE_PREVIEW_PASS_HPP
#define ELIX_ANIMATION_TREE_PREVIEW_PASS_HPP

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Buffer.hpp"
#include "Core/Sampler.hpp"
#include "Core/DescriptorSetLayout.hpp"

#include <glm/glm.hpp>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

struct AnimPreviewDrawData
{
    std::vector<engine::GPUMesh *>  meshes;
    std::vector<engine::Material *> materials;
    std::vector<glm::mat4>          boneMatrices;
    glm::mat4                       modelMatrix{1.0f};
    glm::mat4                       viewMatrix{1.0f};
    glm::mat4                       projMatrix{1.0f};
    bool                            hasData{false};
};

class AnimationTreePreviewPass : public engine::renderGraph::IRenderGraphPass
{
public:
    static constexpr uint32_t PREVIEW_SIZE      = 512;
    static constexpr uint32_t MAX_PREVIEW_BONES = 256;

    AnimationTreePreviewPass();

    void setup(engine::renderGraph::RGPResourcesBuilder &builder) override;
    void compile(engine::renderGraph::RGPResourcesStorage &storage) override;
    void record(core::CommandBuffer::SharedPtr commandBuffer,
                const engine::RenderGraphPassPerFrameData &data,
                const engine::RenderGraphPassContext &renderContext) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(
        const engine::RenderGraphPassContext &renderContext) const override;

    void setPreviewData(const AnimPreviewDrawData &data) { m_pendingData = data; }

    VkImageView getOutputImageView() const;
    VkSampler   getOutputSampler() const;

private:
    struct PreviewCameraUBO
    {
        glm::mat4 view{1.0f};
        glm::mat4 proj{1.0f};
    };

    engine::renderGraph::RGPResourceHandler m_colorHandler{};
    engine::renderGraph::RGPResourceHandler m_depthHandler{};

    const engine::RenderTarget *m_colorTarget{nullptr};
    const engine::RenderTarget *m_depthTarget{nullptr};

    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};
    core::PipelineLayout::SharedPtr      m_pipelineLayout{nullptr};
    core::Sampler::SharedPtr             m_sampler{nullptr};

    // Per-frame camera UBO + bone SSBO
    std::vector<core::Buffer::SharedPtr> m_cameraBuffers;
    std::vector<core::Buffer::SharedPtr> m_boneBuffers;
    std::vector<void *>                  m_cameraMapped;
    std::vector<void *>                  m_boneMapped;
    std::vector<VkDescriptorSet>         m_descriptorSets;
    bool                                 m_descriptorSetsInitialized{false};

    VkViewport           m_viewport{};
    VkRect2D             m_scissor{};
    std::array<VkClearValue, 1> m_clearValues{};

    AnimPreviewDrawData  m_pendingData{};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ANIMATION_TREE_PREVIEW_PASS_HPP
