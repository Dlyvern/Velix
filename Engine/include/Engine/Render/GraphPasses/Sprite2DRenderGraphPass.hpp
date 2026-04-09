#ifndef ELIX_SPRITE2D_RENDER_GRAPH_PASS_HPP
#define ELIX_SPRITE2D_RENDER_GRAPH_PASS_HPP

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"
#include "Core/DescriptorSetLayout.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/Sampler.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Render/RenderTarget.hpp"
#include "Engine/Scene.hpp"
#include "Engine/Texture.hpp"

#include <volk.h>
#include <glm/glm.hpp>

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class Sprite2DRenderGraphPass final : public IRenderGraphPass
{
public:
    struct SpriteGPUData
    {
        glm::vec4 positionAndRotation; // xyz = world pos, w = Z rotation (radians)
        glm::vec2 size;
        float     _pad0{0.f};
        float     _pad1{0.f};
        glm::vec4 color;
        glm::vec4 uvRect;  // u0, v0, u1, v1
        uint32_t  textureIndex{0};
        uint32_t  flipX{0};
        uint32_t  flipY{0};
        uint32_t  _pad2{0};
    };
    static_assert(sizeof(SpriteGPUData) == 80);

    explicit Sprite2DRenderGraphPass(std::vector<RGPResourceHandler> &colorInputHandlers);

    void setScene(Scene *scene) { m_scene = scene; }
    void setExtent(VkExtent2D extent);

    std::vector<RGPResourceHandler> &getHandlers() { return m_outputHandlers; }

    struct Outputs
    {
        RGPOutputSlot<MultiHandle> color;
    } outputs;

    void setup(RGPResourcesBuilder &builder) override;
    void compile(RGPResourcesStorage &storage) override;
    void prepareRecord(const RenderGraphPassPerFrameData &data,
                       const RenderGraphPassContext &ctx) override;
    void record(core::CommandBuffer::SharedPtr cmd,
                const RenderGraphPassPerFrameData &data,
                const RenderGraphPassContext &ctx) override;

    std::vector<RenderPassExecution> getRenderPassExecutions(
        const RenderGraphPassContext &ctx) const override;

    void cleanup() override;

private:
    struct SpritePC
    {
        glm::mat4 viewProj;
        glm::vec3 right;
        float     _pad0{0.f};
        glm::vec3 up;
        float     _pad1{0.f};
    };
    static_assert(sizeof(SpritePC) == 96);

    Scene *m_scene{nullptr};

    std::vector<RGPResourceHandler> &m_colorInputHandlers;
    std::vector<RGPResourceHandler>  m_outputHandlers;

    static constexpr uint32_t MAX_SPRITES          = 10'000;
    static constexpr uint32_t MAX_SPRITE_TEXTURES  = 8;
    static constexpr VkDeviceSize SSBO_SIZE = MAX_SPRITES * sizeof(SpriteGPUData);

    VkFormat   m_format{VK_FORMAT_R16G16B16A16_SFLOAT};
    VkExtent2D m_extent{};
    VkViewport m_viewport{};
    VkRect2D   m_scissor{};

    std::vector<const RenderTarget *> m_outputRenderTargets;
    std::vector<const RenderTarget *> m_inputRenderTargets;
    std::vector<core::Buffer::SharedPtr> m_spriteSSBOs;

    std::vector<VkDescriptorSet> m_ssboSets;
    std::vector<VkDescriptorSet> m_passthroughSets;
    std::vector<VkDescriptorSet> m_textureSets;

    core::DescriptorSetLayout::SharedPtr m_ssboDescriptorSetLayout;
    core::DescriptorSetLayout::SharedPtr m_passthroughDescriptorSetLayout;
    core::DescriptorSetLayout::SharedPtr m_textureDescriptorSetLayout;

    core::PipelineLayout::SharedPtr m_spritePipelineLayout;
    core::PipelineLayout::SharedPtr m_passthroughPipelineLayout;

    core::Sampler::SharedPtr m_nearestSampler;
    core::Sampler::SharedPtr m_linearSampler;

    std::unordered_map<std::string, Texture::SharedPtr> m_textureCache;
    Texture::SharedPtr m_defaultWhiteTexture;

    bool m_compiled{false};

    std::vector<SpritePC>   m_preparedPushConstants;
    std::vector<uint32_t>   m_preparedVertexCounts;

    void collectSpriteData(std::vector<SpriteGPUData> &out,
                           std::vector<std::string>   &outTextureSlots) const;
    void recordPassthrough(core::CommandBuffer::SharedPtr cmd, uint32_t imageIndex);
    void recordSprites(core::CommandBuffer::SharedPtr cmd, uint32_t imageIndex);
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SPRITE2D_RENDER_GRAPH_PASS_HPP
