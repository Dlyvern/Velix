#include "Engine/Render/GraphPasses/Sprite2DRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/Memory/MemoryFlags.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Components/SpriteComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Entity.hpp"
#include "Engine/Assets/AssetsLoader.hpp"

#include <algorithm>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

Sprite2DRenderGraphPass::Sprite2DRenderGraphPass(std::vector<RGPResourceHandler> &colorInputHandlers)
    : m_colorInputHandlers(colorInputHandlers)
{
    setDebugName("Sprite2D render graph pass");
    outputs.color.setOwner(this);
}

void Sprite2DRenderGraphPass::setExtent(VkExtent2D extent)
{
    if (m_extent.width == extent.width && m_extent.height == extent.height)
        return;

    m_extent = extent;
    m_viewport = {0.f, 0.f,
                  static_cast<float>(extent.width),
                  static_cast<float>(extent.height),
                  0.f, 1.f};
    m_scissor = {{0, 0}, extent};
    requestRecompilation();
}

void Sprite2DRenderGraphPass::setup(RGPResourcesBuilder &builder)
{
    for (auto &h : m_colorInputHandlers)
        builder.read(h, RGPTextureUsage::SAMPLED);

    if (m_extent.width == 0)
        m_extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();

    m_viewport = {0.f, 0.f,
                  static_cast<float>(m_extent.width),
                  static_cast<float>(m_extent.height),
                  0.f, 1.f};
    m_scissor = {{0, 0}, m_extent};

    m_format = VK_FORMAT_R16G16B16A16_SFLOAT;

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this] { return m_extent; });

    m_outputHandlers.clear();
    const uint32_t imageCount = static_cast<uint32_t>(m_colorInputHandlers.size());
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_SPRITES_OUT_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT);
    }
    outputs.color.set(m_outputHandlers);

    auto device = core::VulkanContext::getContext()->getDevice();

    // Passthrough: blit input into output
    VkDescriptorSetLayoutBinding passthroughBinding{};
    passthroughBinding.binding         = 0;
    passthroughBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    passthroughBinding.descriptorCount = 1;
    passthroughBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_passthroughDescriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{passthroughBinding});

    m_passthroughPipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_passthroughDescriptorSetLayout},
        std::vector<VkPushConstantRange>{});

    // SSBO set (set 0)
    VkDescriptorSetLayoutBinding ssboBinding{};
    ssboBinding.binding         = 0;
    ssboBinding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ssboBinding.descriptorCount = 1;
    ssboBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    m_ssboDescriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{ssboBinding});

    // Texture array (set 1)
    VkDescriptorSetLayoutBinding texArrayBinding{};
    texArrayBinding.binding         = 0;
    texArrayBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texArrayBinding.descriptorCount = MAX_SPRITE_TEXTURES;
    texArrayBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_textureDescriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{texArrayBinding});

    m_spritePipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{
            *m_ssboDescriptorSetLayout,
            *m_textureDescriptorSetLayout},
        std::vector<VkPushConstantRange>{
            PushConstant<SpritePC>::getRange(VK_SHADER_STAGE_VERTEX_BIT)});

    m_nearestSampler = core::Sampler::createShared(
        VK_FILTER_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    m_linearSampler = core::Sampler::createShared(
        VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void Sprite2DRenderGraphPass::compile(RGPResourcesStorage &storage)
{
    const uint32_t imageCount = static_cast<uint32_t>(m_outputHandlers.size());

    if (!m_defaultWhiteTexture)
        m_defaultWhiteTexture = Texture::getDefaultWhiteTexture();

    m_outputRenderTargets.resize(imageCount);
    m_inputRenderTargets.resize(imageCount);
    m_spriteSSBOs.resize(imageCount);
    m_ssboSets.resize(imageCount, VK_NULL_HANDLE);
    m_passthroughSets.resize(imageCount, VK_NULL_HANDLE);
    m_textureSets.resize(imageCount, VK_NULL_HANDLE);
    m_preparedPushConstants.resize(imageCount);
    m_preparedVertexCounts.resize(imageCount, 0u);

    auto device = core::VulkanContext::getContext()->getDevice();
    auto pool   = core::VulkanContext::getContext()->getPersistentDescriptorPool();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputRenderTargets[i] = storage.getTexture(m_outputHandlers[i]);
        m_inputRenderTargets[i]  = storage.getTexture(m_colorInputHandlers[i]);

        m_spriteSSBOs[i] = core::Buffer::createShared(
            SSBO_SIZE,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU);

        if (!m_compiled)
        {
            m_ssboSets[i] = DescriptorSetBuilder::begin()
                                .addBuffer(m_spriteSSBOs[i], SSBO_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                .build(device, pool, m_ssboDescriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addBuffer(m_spriteSSBOs[i], SSBO_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .update(device, m_ssboSets[i]);
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

        // Pre-fill texture array with the default white texture
        {
            std::array<VkDescriptorImageInfo, MAX_SPRITE_TEXTURES> imageInfos{};
            for (uint32_t slot = 0; slot < MAX_SPRITE_TEXTURES; ++slot)
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
            write.descriptorCount = MAX_SPRITE_TEXTURES;
            write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo      = imageInfos.data();
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }

    m_compiled = true;
    recompilationIsDone();
}

void Sprite2DRenderGraphPass::prepareRecord(const RenderGraphPassPerFrameData &data,
                                            const RenderGraphPassContext &ctx)
{
    if (ctx.currentImageIndex >= m_ssboSets.size())
        return;

    const uint32_t imageIndex = ctx.currentImageIndex;
    m_preparedVertexCounts[imageIndex] = 0u;

    static thread_local std::vector<SpriteGPUData> gpuSprites;
    static thread_local std::vector<std::string>   textureSlots;
    gpuSprites.clear();
    textureSlots.clear();

    collectSpriteData(gpuSprites, textureSlots);

    if (gpuSprites.empty())
        return;

    // Sort by sortLayer (ascending) — we don't have per-GPU-data sortLayer here;
    // sorting is done on CPU before upload (collectSpriteData already fills in order)

    // Update texture descriptor array
    auto device = core::VulkanContext::getContext()->getDevice();

    std::array<VkDescriptorImageInfo, MAX_SPRITE_TEXTURES> imageInfos{};
    for (uint32_t slot = 0; slot < MAX_SPRITE_TEXTURES; ++slot)
    {
        Texture *tex = m_defaultWhiteTexture.get();

        if (slot < static_cast<uint32_t>(textureSlots.size()) && !textureSlots[slot].empty())
        {
            const std::string &path = textureSlots[slot];
            auto it = m_textureCache.find(path);
            if (it != m_textureCache.end())
            {
                tex = it->second.get();
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
    write.descriptorCount = MAX_SPRITE_TEXTURES;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = imageInfos.data();
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    const VkDeviceSize uploadSize = gpuSprites.size() * sizeof(SpriteGPUData);
    m_spriteSSBOs[imageIndex]->upload(gpuSprites.data(), uploadSize);

    const glm::vec3 cameraRight(data.view[0][0], data.view[1][0], data.view[2][0]);
    const glm::vec3 cameraUp(data.view[0][1], data.view[1][1], data.view[2][1]);

    SpritePC pc{};
    pc.viewProj = data.projection * data.view;
    pc.right    = cameraRight;
    pc.up       = cameraUp;

    m_preparedPushConstants[imageIndex] = pc;
    m_preparedVertexCounts[imageIndex]  = static_cast<uint32_t>(gpuSprites.size()) * 6u;
}

void Sprite2DRenderGraphPass::record(core::CommandBuffer::SharedPtr cmd,
                                     const RenderGraphPassPerFrameData &data,
                                     const RenderGraphPassContext &ctx)
{
    (void)data;
    vkCmdSetViewport(cmd->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(cmd->vk(), 0, 1, &m_scissor);

    recordPassthrough(cmd, ctx.currentImageIndex);
    recordSprites(cmd, ctx.currentImageIndex);
}

std::vector<IRenderGraphPass::RenderPassExecution>
Sprite2DRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &ctx) const
{
    if (m_outputRenderTargets.empty() ||
        ctx.currentImageIndex >= m_outputRenderTargets.size() ||
        !m_outputRenderTargets[ctx.currentImageIndex])
        return {};

    RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView   = m_outputRenderTargets[ctx.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue  = {{{0.f, 0.f, 0.f, 1.f}}};

    exec.colorsRenderingItems = {color};
    exec.useDepth   = false;
    exec.colorFormats = {m_format};
    exec.depthFormat  = VK_FORMAT_UNDEFINED;

    exec.targets[m_outputHandlers[ctx.currentImageIndex]] =
        m_outputRenderTargets[ctx.currentImageIndex];

    return {exec};
}

void Sprite2DRenderGraphPass::cleanup()
{
    m_spriteSSBOs.clear();
    m_outputRenderTargets.clear();
    m_inputRenderTargets.clear();
}

void Sprite2DRenderGraphPass::collectSpriteData(std::vector<SpriteGPUData> &out,
                                                 std::vector<std::string>   &outTextureSlots) const
{
    if (!m_scene)
        return;

    outTextureSlots.clear();
    outTextureSlots.push_back(""); // slot 0 = default white

    std::unordered_map<std::string, uint32_t> textureToSlot;

    // Collect sprites into a temporary buffer so we can sort by sortLayer
    struct SortEntry
    {
        SpriteGPUData gpu;
        int sortLayer;
    };
    static thread_local std::vector<SortEntry> entries;
    entries.clear();

    for (const auto &entity : m_scene->getEntities())
    {
        if (!entity || !entity->isEnabled())
            continue;

        auto *sprite = entity->getComponent<SpriteComponent>();
        if (!sprite || !sprite->visible)
            continue;

        auto *transform = entity->getComponent<Transform3DComponent>();
        const glm::vec3 worldPos = transform ? transform->getWorldPosition() : glm::vec3(0.f);

        uint32_t slot = 0;
        if (!sprite->texturePath.empty())
        {
            auto it = textureToSlot.find(sprite->texturePath);
            if (it != textureToSlot.end())
            {
                slot = it->second;
            }
            else if (outTextureSlots.size() < MAX_SPRITE_TEXTURES)
            {
                slot = static_cast<uint32_t>(outTextureSlots.size());
                textureToSlot[sprite->texturePath] = slot;
                outTextureSlots.push_back(sprite->texturePath);
            }
        }

        SpriteGPUData gpu{};
        gpu.positionAndRotation = glm::vec4(worldPos, sprite->rotation);
        gpu.size                = sprite->size;
        gpu.color               = sprite->color;
        gpu.uvRect              = sprite->uvRect;
        gpu.textureIndex        = slot;
        gpu.flipX               = sprite->flipX ? 1u : 0u;
        gpu.flipY               = sprite->flipY ? 1u : 0u;

        entries.push_back({gpu, sprite->sortLayer});
    }

    // Sort by sortLayer ascending
    std::stable_sort(entries.begin(), entries.end(), [](const SortEntry &a, const SortEntry &b)
    {
        return a.sortLayer < b.sortLayer;
    });

    out.reserve(entries.size());
    for (auto &e : entries)
    {
        if (out.size() >= MAX_SPRITES)
            break;
        out.push_back(e.gpu);
    }
}

void Sprite2DRenderGraphPass::recordPassthrough(core::CommandBuffer::SharedPtr cmd, uint32_t imageIndex)
{
    if (imageIndex >= m_passthroughSets.size() || !m_passthroughSets[imageIndex])
        return;

    GraphicsPipelineKey key{};
    key.shader       = ShaderId::Present;
    key.blend        = BlendMode::None;
    key.cull         = CullMode::None;
    key.depthTest    = false;
    key.depthWrite   = false;
    key.colorFormats = {m_format};
    key.pipelineLayout = m_passthroughPipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(cmd->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(cmd->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_passthroughPipelineLayout, 0, 1,
                            &m_passthroughSets[imageIndex], 0, nullptr);

    profiling::cmdDraw(cmd, 3, 1, 0, 0);
}

void Sprite2DRenderGraphPass::recordSprites(core::CommandBuffer::SharedPtr cmd, uint32_t imageIndex)
{
    if (imageIndex >= m_ssboSets.size())
        return;

    const uint32_t vertexCount = imageIndex < m_preparedVertexCounts.size()
                                     ? m_preparedVertexCounts[imageIndex]
                                     : 0u;
    if (vertexCount == 0u)
        return;

    GraphicsPipelineKey key{};
    key.shader       = ShaderId::Sprite2D;
    key.blend        = BlendMode::AlphaBlend;
    key.cull         = CullMode::None;
    key.depthTest    = false;
    key.depthWrite   = false;
    key.colorFormats = {m_format};
    key.pipelineLayout = m_spritePipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(cmd->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(cmd->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_spritePipelineLayout, 0, 1,
                            &m_ssboSets[imageIndex], 0, nullptr);

    vkCmdBindDescriptorSets(cmd->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_spritePipelineLayout, 1, 1,
                            &m_textureSets[imageIndex], 0, nullptr);

    const SpritePC &pc = m_preparedPushConstants[imageIndex];
    vkCmdPushConstants(cmd->vk(), m_spritePipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(SpritePC), &pc);

    profiling::cmdDraw(cmd, vertexCount, 1, 0, 0);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
