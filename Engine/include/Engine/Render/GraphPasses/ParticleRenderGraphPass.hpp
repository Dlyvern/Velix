#ifndef ELIX_PARTICLE_RENDER_GRAPH_PASS_HPP
#define ELIX_PARTICLE_RENDER_GRAPH_PASS_HPP

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"
#include "Core/DescriptorSetLayout.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/RenderTarget.hpp"
#include "Engine/Scene.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

#include <volk.h>
#include <glm/glm.hpp>

#include <vector>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class ParticleRenderGraphPass final : public IRenderGraphPass
{
public:
    explicit ParticleRenderGraphPass(std::vector<RGPResourceHandler> &colorInputHandlers,
                                     RGPResourceHandler *depthInputHandler = nullptr);

    void setScene(Scene *scene) { m_scene = scene; }
    void setExtent(VkExtent2D extent);

    std::vector<RGPResourceHandler> &getHandlers() { return m_outputHandlers; }

    void setup(RGPResourcesBuilder &builder) override;
    void compile(RGPResourcesStorage &storage) override;
    void record(core::CommandBuffer::SharedPtr cmd,
                const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &ctx) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(
        const RenderGraphPassContext &ctx) const override;

    void cleanup() override;

private:
    struct ParticlePC
    {
        glm::mat4 viewProj;
        glm::vec3 right;
        float _pad0;
        glm::vec3 up;
        float _pad1;
    };
    static_assert(sizeof(ParticlePC) == 96);

    Scene *m_scene{nullptr};

    std::vector<RGPResourceHandler> &m_colorInputHandlers;
    RGPResourceHandler *m_depthInputHandler{nullptr};
    std::vector<RGPResourceHandler> m_outputHandlers;

    VkFormat m_format{VK_FORMAT_R16G16B16A16_SFLOAT};
    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D m_scissor{};

    std::vector<const RenderTarget *> m_outputRenderTargets;
    std::vector<const RenderTarget *> m_inputRenderTargets;
    std::vector<core::Buffer::SharedPtr> m_particleSSBOs;

    std::vector<VkDescriptorSet> m_descriptorSets;
    std::vector<VkDescriptorSet> m_passthroughSets;

    core::DescriptorSetLayout::SharedPtr m_ssboDescriptorSetLayout;
    core::DescriptorSetLayout::SharedPtr m_passthroughDescriptorSetLayout;
    core::PipelineLayout::SharedPtr m_particlePipelineLayout;
    core::PipelineLayout::SharedPtr m_passthroughPipelineLayout;
    core::Sampler::SharedPtr m_nearestSampler;

    bool m_compiled{false};

    void collectParticleData(std::vector<ParticleGPUData> &out,
                             const glm::vec3 &cameraRight,
                             const glm::vec3 &cameraUp) const;
    void recordPassthrough(core::CommandBuffer::SharedPtr cmd, uint32_t imageIndex);
    void recordParticles(core::CommandBuffer::SharedPtr cmd,
                         const RenderGraphPassPerFrameData &data,
                         uint32_t imageIndex);
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PARTICLE_RENDER_GRAPH_PASS_HPP
