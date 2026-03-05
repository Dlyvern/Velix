#include "Engine/Render/GraphPasses/ParticleRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/Memory/MemoryFlags.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Components/ParticleSystemComponent.hpp"
#include "Engine/Particles/Modules/RendererModule.hpp"
#include "Engine/Entity.hpp"
#include "Engine/Assets/AssetsLoader.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

static constexpr uint32_t MAX_GPU_PARTICLES = 100'000;
static constexpr VkDeviceSize SSBO_SIZE = MAX_GPU_PARTICLES * sizeof(ParticleGPUData);

ParticleRenderGraphPass::ParticleRenderGraphPass(std::vector<RGPResourceHandler> &colorInputHandlers,
                                                 RGPResourceHandler *depthInputHandler)
    : m_colorInputHandlers(colorInputHandlers), m_depthInputHandler(depthInputHandler)
{
    setDebugName("Particle render graph pass");
}

void ParticleRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = {0.f, 0.f,
                  static_cast<float>(extent.width),
                  static_cast<float>(extent.height),
                  0.f, 1.f};
    m_scissor = {{0, 0}, extent};
    requestRecompilation();
}

void ParticleRenderGraphPass::setup(RGPResourcesBuilder &builder)
{
    for (auto &h : m_colorInputHandlers)
        builder.read(h, RGPTextureUsage::SAMPLED);

    if (m_depthInputHandler)
        builder.read(*m_depthInputHandler, RGPTextureUsage::SAMPLED);

    if (m_extent.width == 0)
        m_extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();

    m_viewport = {0.f, 0.f,
                  static_cast<float>(m_extent.width),
                  static_cast<float>(m_extent.height),
                  0.f, 1.f};
    m_scissor = {{0, 0}, m_extent};

    m_format = VK_FORMAT_R16G16B16A16_SFLOAT; // default; overridden if needed

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this]
                                    { return m_extent; });

    m_outputHandlers.clear();
    const uint32_t imageCount = static_cast<uint32_t>(m_colorInputHandlers.size());
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_PARTICLES_OUT_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT);
    }

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding passthroughBinding{};
    passthroughBinding.binding = 0;
    passthroughBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    passthroughBinding.descriptorCount = 1;
    passthroughBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_passthroughDescriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{passthroughBinding});

    m_passthroughPipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_passthroughDescriptorSetLayout},
        std::vector<VkPushConstantRange>{});

    VkDescriptorSetLayoutBinding ssboBinding{};
    ssboBinding.binding = 0;
    ssboBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ssboBinding.descriptorCount = 1;
    ssboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    m_ssboDescriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{ssboBinding});

    // Texture array: set 1, binding 0 — up to MAX_PARTICLE_TEXTURES textures per frame
    VkDescriptorSetLayoutBinding textureArrayBinding{};
    textureArrayBinding.binding = 0;
    textureArrayBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textureArrayBinding.descriptorCount = MAX_PARTICLE_TEXTURES;
    textureArrayBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_textureDescriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{textureArrayBinding});

    m_particlePipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
            *m_ssboDescriptorSetLayout,
            *m_textureDescriptorSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<ParticlePC>::getRange(VK_SHADER_STAGE_VERTEX_BIT)});

    m_nearestSampler = core::Sampler::createShared(
        VK_FILTER_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    m_linearSampler = core::Sampler::createShared(
        VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void ParticleRenderGraphPass::compile(RGPResourcesStorage &storage)
{
    const uint32_t imageCount = static_cast<uint32_t>(m_outputHandlers.size());

    if (!m_defaultWhiteTexture)
        m_defaultWhiteTexture = Texture::getDefaultWhiteTexture();

    m_outputRenderTargets.resize(imageCount);
    m_inputRenderTargets.resize(imageCount);
    m_particleSSBOs.resize(imageCount);
    m_descriptorSets.resize(imageCount, VK_NULL_HANDLE);
    m_passthroughSets.resize(imageCount, VK_NULL_HANDLE);
    m_textureSets.resize(imageCount, VK_NULL_HANDLE);

    auto device = core::VulkanContext::getContext()->getDevice();
    auto pool = core::VulkanContext::getContext()->getPersistentDescriptorPool();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputRenderTargets[i] = storage.getTexture(m_outputHandlers[i]);
        m_inputRenderTargets[i] = storage.getTexture(m_colorInputHandlers[i]);

        m_particleSSBOs[i] = core::Buffer::createShared(
            SSBO_SIZE,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU);

        if (!m_compiled)
        {
            m_descriptorSets[i] = DescriptorSetBuilder::begin()
                                      .addBuffer(m_particleSSBOs[i], SSBO_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                      .build(device, pool, m_ssboDescriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addBuffer(m_particleSSBOs[i], SSBO_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .update(device, m_descriptorSets[i]);
        }

        if (m_inputRenderTargets[i])
        {
            if (!m_compiled)
            {
                m_passthroughSets[i] = DescriptorSetBuilder::begin()
                                           .addImage(m_inputRenderTargets[i]->vkImageView(),
                                                     m_nearestSampler->vk(),
                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                           .build(device, pool, m_passthroughDescriptorSetLayout);
            }
            else
            {
                DescriptorSetBuilder::begin()
                    .addImage(m_inputRenderTargets[i]->vkImageView(),
                              m_nearestSampler->vk(),
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                    .update(device, m_passthroughSets[i]);
            }
        }

        // Build texture array descriptor set pre-filled with the default white texture.
        // The actual textures are written per-frame in recordParticles() before the draw.
        {
            std::array<VkDescriptorImageInfo, MAX_PARTICLE_TEXTURES> imageInfos{};
            for (uint32_t slot = 0; slot < MAX_PARTICLE_TEXTURES; ++slot)
            {
                imageInfos[slot].sampler     = m_linearSampler->vk();
                imageInfos[slot].imageView   = m_defaultWhiteTexture->vkImageView();
                imageInfos[slot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            if (!m_compiled)
            {
                VkDescriptorSetLayout layout = m_textureDescriptorSetLayout->vk();
                VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                allocInfo.descriptorPool     = pool;
                allocInfo.descriptorSetCount = 1;
                allocInfo.pSetLayouts        = &layout;
                vkAllocateDescriptorSets(device, &allocInfo, &m_textureSets[i]);
            }

            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet          = m_textureSets[i];
            write.dstBinding      = 0;
            write.dstArrayElement = 0;
            write.descriptorCount = MAX_PARTICLE_TEXTURES;
            write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo      = imageInfos.data();
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }

    m_compiled = true;
    recompilationIsDone();
}

void ParticleRenderGraphPass::record(core::CommandBuffer::SharedPtr cmd,
                                     const RenderGraphPassPerFrameData &data,
                                     const RenderGraphPassContext &ctx)
{
    vkCmdSetViewport(cmd->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(cmd->vk(), 0, 1, &m_scissor);

    recordPassthrough(cmd, ctx.currentImageIndex);
    recordParticles(cmd, data, ctx.currentImageIndex);
}

std::vector<IRenderGraphPass::RenderPassExecution>
ParticleRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &ctx) const
{
    if (m_outputRenderTargets.empty() ||
        ctx.currentImageIndex >= m_outputRenderTargets.size() ||
        !m_outputRenderTargets[ctx.currentImageIndex])
        return {};

    RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_outputRenderTargets[ctx.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = {{{0.f, 0.f, 0.f, 1.f}}};

    exec.colorsRenderingItems = {color};
    exec.useDepth = false;
    exec.colorFormats = {m_format};
    exec.depthFormat = VK_FORMAT_UNDEFINED;

    exec.targets[m_outputHandlers[ctx.currentImageIndex]] =
        m_outputRenderTargets[ctx.currentImageIndex];

    return {exec};
}

void ParticleRenderGraphPass::cleanup()
{
    m_particleSSBOs.clear();
    m_outputRenderTargets.clear();
    m_inputRenderTargets.clear();
}

void ParticleRenderGraphPass::collectParticleData(std::vector<ParticleGPUData> &out,
                                                  std::vector<std::string> &outTextureSlots,
                                                  const glm::vec3 &cameraRight,
                                                  const glm::vec3 &cameraUp) const
{
    if (!m_scene)
        return;

    // Slot 0 is always the default white texture (untextured particles).
    outTextureSlots.clear();
    outTextureSlots.push_back("");

    std::unordered_map<std::string, uint32_t> textureToSlot;

    for (const auto &entity : m_scene->getEntities())
    {
        if (!entity || !entity->isEnabled())
            continue;

        for (auto *comp : entity->getComponents<ParticleSystemComponent>())
        {
            if (!comp)
                continue;
            const ParticleSystem *ps = comp->getParticleSystem();
            if (!ps)
                continue;

            for (const auto &emitter : ps->getEmitters())
            {
                if (!emitter || !emitter->enabled)
                    continue;

                const auto *rm = emitter->getModule<RendererModule>();

                // Determine which texture slot this emitter uses.
                uint32_t slot = 0;
                if (rm && !rm->texturePath.empty())
                {
                    auto it = textureToSlot.find(rm->texturePath);
                    if (it != textureToSlot.end())
                    {
                        slot = it->second;
                    }
                    else if (outTextureSlots.size() < MAX_PARTICLE_TEXTURES)
                    {
                        slot = static_cast<uint32_t>(outTextureSlots.size());
                        textureToSlot[rm->texturePath] = slot;
                        outTextureSlots.push_back(rm->texturePath);
                    }
                    // else: too many unique textures → fall back to slot 0
                }

                for (const Particle &p : emitter->getParticles())
                {
                    if (!p.alive)
                        continue;
                    if (out.size() >= MAX_GPU_PARTICLES)
                        return;

                    ParticleGPUData gpu{};
                    float rotation = p.rotation;
                    if (rm && rm->facingMode == ParticleFacingMode::VelocityAligned)
                    {
                        const float vx = glm::dot(p.velocity, cameraRight);
                        const float vy = glm::dot(p.velocity, cameraUp);
                        const float len2 = vx * vx + vy * vy;
                        if (len2 > 1e-8f)
                            rotation = std::atan2(-vx, vy);
                    }

                    gpu.positionAndRotation = glm::vec4(p.position, rotation);
                    gpu.color               = p.color;
                    gpu.size                = p.size;
                    gpu.textureIndex        = slot;
                    out.push_back(gpu);
                }
            }
        }
    }
}

void ParticleRenderGraphPass::recordPassthrough(core::CommandBuffer::SharedPtr cmd, uint32_t imageIndex)
{
    if (imageIndex >= m_passthroughSets.size() || !m_passthroughSets[imageIndex])
        return;

    GraphicsPipelineKey key{};
    key.shader = ShaderId::Present; // fullscreen blit
    key.blend = BlendMode::None;
    key.cull = CullMode::None;
    key.depthTest = false;
    key.depthWrite = false;
    key.colorFormats = {m_format};
    key.pipelineLayout = m_passthroughPipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(cmd->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(cmd->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_passthroughPipelineLayout, 0, 1,
                            &m_passthroughSets[imageIndex], 0, nullptr);

    profiling::cmdDraw(cmd, 3, 1, 0, 0);
}

void ParticleRenderGraphPass::recordParticles(core::CommandBuffer::SharedPtr cmd,
                                              const RenderGraphPassPerFrameData &data,
                                              uint32_t imageIndex)
{
    if (imageIndex >= m_descriptorSets.size())
        return;

    static thread_local std::vector<ParticleGPUData> gpuParticles;
    static thread_local std::vector<std::string> textureSlots;
    gpuParticles.clear();
    textureSlots.clear();

    const glm::vec3 cameraRight(data.view[0][0], data.view[1][0], data.view[2][0]);
    const glm::vec3 cameraUp(data.view[0][1], data.view[1][1], data.view[2][1]);
    collectParticleData(gpuParticles, textureSlots, cameraRight, cameraUp);

    if (gpuParticles.empty())
        return;

    // Resolve textures for each slot and update the texture array descriptor set.
    std::array<VkDescriptorImageInfo, MAX_PARTICLE_TEXTURES> imageInfos{};
    for (uint32_t slot = 0; slot < MAX_PARTICLE_TEXTURES; ++slot)
    {
        Texture *tex = m_defaultWhiteTexture.get();

        if (slot < static_cast<uint32_t>(textureSlots.size()) && !textureSlots[slot].empty())
        {
            const std::string &path = textureSlots[slot];
            auto cacheIt = m_textureCache.find(path);
            if (cacheIt != m_textureCache.end())
            {
                tex = cacheIt->second.get();
            }
            else
            {
                auto loaded = AssetsLoader::loadTextureGPU(path);
                if (loaded)
                {
                    m_textureCache[path] = loaded;
                    tex = loaded.get();
                }
            }
        }

        imageInfos[slot].sampler     = m_linearSampler->vk();
        imageInfos[slot].imageView   = tex ? tex->vkImageView() : m_defaultWhiteTexture->vkImageView();
        imageInfos[slot].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet          = m_textureSets[imageIndex];
    write.dstBinding      = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = MAX_PARTICLE_TEXTURES;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = imageInfos.data();
    vkUpdateDescriptorSets(core::VulkanContext::getContext()->getDevice(), 1, &write, 0, nullptr);

    const VkDeviceSize uploadSize = gpuParticles.size() * sizeof(ParticleGPUData);
    m_particleSSBOs[imageIndex]->upload(gpuParticles.data(), uploadSize);

    GraphicsPipelineKey key{};
    key.shader = ShaderId::Particle;
    key.blend = BlendMode::AlphaBlend;
    key.cull = CullMode::None;
    key.depthTest = false;
    key.depthWrite = false;
    key.colorFormats = {m_format};
    key.pipelineLayout = m_particlePipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(cmd->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(cmd->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_particlePipelineLayout, 0, 1,
                            &m_descriptorSets[imageIndex], 0, nullptr);

    vkCmdBindDescriptorSets(cmd->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_particlePipelineLayout, 1, 1,
                            &m_textureSets[imageIndex], 0, nullptr);

    ParticlePC pc{};
    pc.viewProj = data.projection * data.view;
    pc.right    = cameraRight;
    pc.up       = cameraUp;

    vkCmdPushConstants(cmd->vk(), m_particlePipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(ParticlePC), &pc);

    const uint32_t vertCount = static_cast<uint32_t>(gpuParticles.size()) * 6;
    profiling::cmdDraw(cmd, vertCount, 1, 0, 0);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
