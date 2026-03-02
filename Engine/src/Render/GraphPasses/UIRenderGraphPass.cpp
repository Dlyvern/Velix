#include "Engine/Render/GraphPasses/UIRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/Logger.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Vertex.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <limits>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

namespace
{
    glm::vec2 rotatePointAroundPivot(const glm::vec2 &point, const glm::vec2 &pivot, float rotationDegrees)
    {
        if (std::abs(rotationDegrees) <= std::numeric_limits<float>::epsilon())
            return point;

        const float rotationRadians = glm::radians(rotationDegrees);
        const float cosAngle = std::cos(rotationRadians);
        const float sinAngle = std::sin(rotationRadians);

        const glm::vec2 delta = point - pivot;
        return pivot + glm::vec2(
                           delta.x * cosAngle - delta.y * sinAngle,
                           delta.x * sinAngle + delta.y * cosAngle);
    }

    void appendTexturedQuad(std::vector<vertex::Vertex2D> &vertices,
                            const glm::vec2 &p0,
                            const glm::vec2 &p1,
                            const glm::vec2 &p2,
                            const glm::vec2 &p3,
                            const ui::FontAtlas::GlyphUV &uvRect)
    {
        // Use V as-is from the atlas rect (v0: top, v1: bottom) to avoid upside-down glyphs.
        vertices.push_back({{p0.x, p0.y, 0.0f}, {uvRect.u0, uvRect.v0}});
        vertices.push_back({{p1.x, p1.y, 0.0f}, {uvRect.u1, uvRect.v0}});
        vertices.push_back({{p2.x, p2.y, 0.0f}, {uvRect.u0, uvRect.v1}});

        vertices.push_back({{p1.x, p1.y, 0.0f}, {uvRect.u1, uvRect.v0}});
        vertices.push_back({{p3.x, p3.y, 0.0f}, {uvRect.u1, uvRect.v1}});
        vertices.push_back({{p2.x, p2.y, 0.0f}, {uvRect.u0, uvRect.v1}});
    }
} // namespace

UIRenderGraphPass::UIRenderGraphPass(std::vector<RGPResourceHandler> &inputHandlers)
    : m_inputHandlers(inputHandlers)
{
    setDebugName("UI render graph pass");
    m_clearValues[0].color = {0.f, 0.f, 0.f, 1.f};
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void UIRenderGraphPass::setup(RGPResourcesBuilder &builder)
{
    m_format = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();

    for (auto &h : m_inputHandlers)
        builder.read(h, RGPTextureUsage::SAMPLED);

    RGPTextureDescription outDesc{m_format, RGPTextureUsage::COLOR_ATTACHMENT};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this]
                                    { return m_extent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_UI_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, RGPTextureUsage::COLOR_ATTACHMENT);
    }

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_textureDescriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{samplerBinding});

    m_billboardPipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_textureDescriptorSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<BillboardPC>::getRange(VK_SHADER_STAGE_VERTEX_BIT)});

    m_textPipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_textureDescriptorSetLayout},
        std::vector<VkPushConstantRange>{PushConstant<UITextPC>::getRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_quadPipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{},
        std::vector<VkPushConstantRange>{PushConstant<UIQuadPC>::getRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)});

    m_passthroughPipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_textureDescriptorSetLayout},
        std::vector<VkPushConstantRange>{});

    m_nearestSampler = core::Sampler::createShared(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
    m_linearSampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_INT_OPAQUE_BLACK);
}

void UIRenderGraphPass::compile(RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    m_outputRenderTargets.resize(imageCount);
    m_passthroughDescriptorSets.resize(imageCount, VK_NULL_HANDLE);
    m_transientVertexBuffersByFrame.resize(std::max<size_t>(2u, static_cast<size_t>(imageCount)));

    auto device = core::VulkanContext::getContext()->getDevice();
    auto pool = core::VulkanContext::getContext()->getPersistentDescriptorPool();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputRenderTargets[i] = storage.getTexture(m_outputHandlers[i]);

        // Build or update passthrough descriptor set for this frame's input texture.
        const auto *inputTex = storage.getTexture(m_inputHandlers[i]);
        if (!m_passthroughSetsBuilt)
        {
            m_passthroughDescriptorSets[i] = DescriptorSetBuilder::begin()
                                                 .addImage(inputTex->vkImageView(), m_nearestSampler->vk(),
                                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                                 .build(device, pool, m_textureDescriptorSetLayout);
        }
        else
        {
            DescriptorSetBuilder::begin()
                .addImage(inputTex->vkImageView(), m_nearestSampler->vk(),
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .update(device, m_passthroughDescriptorSets[i]);
        }
    }
    m_passthroughSetsBuilt = true;
}

void UIRenderGraphPass::recordPassthrough(core::CommandBuffer::SharedPtr commandBuffer,
                                          uint32_t frameIndex)
{
    if (frameIndex >= m_passthroughDescriptorSets.size())
        return;

    GraphicsPipelineKey key{};
    key.shader = ShaderId::Present;
    key.blend = BlendMode::None;
    key.cull = CullMode::None;
    key.depthTest = false;
    key.depthWrite = false;
    key.colorFormats = {m_format};
    key.pipelineLayout = m_passthroughPipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkDescriptorSet ds = m_passthroughDescriptorSets[frameIndex];
    vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_passthroughPipelineLayout, 0, 1, &ds, 0, nullptr);

    profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);
}

VkDescriptorSet UIRenderGraphPass::getTextureDescriptorSet(VkImageView view, VkSampler sampler)
{
    auto it = m_texDescriptorSets.find(view);
    if (it != m_texDescriptorSets.end())
        return it->second;

    VkDescriptorSet ds = DescriptorSetBuilder::begin()
                             .addImage(view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                             .build(core::VulkanContext::getContext()->getDevice(),
                                    core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                    m_textureDescriptorSetLayout);
    m_texDescriptorSets[view] = ds;
    return ds;
}

void UIRenderGraphPass::recordBillboards(core::CommandBuffer::SharedPtr commandBuffer,
                                         const RenderGraphPassPerFrameData &data)
{
    if (m_renderData.billboards.empty())
        return;

    GraphicsPipelineKey key{};
    key.shader = ShaderId::Billboard;
    key.blend = BlendMode::AlphaBlend;
    key.cull = CullMode::None;
    key.depthTest = false;
    key.depthWrite = false;
    key.colorFormats = {m_format};
    key.pipelineLayout = m_billboardPipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    const glm::mat4 viewProj = data.projection * data.view;
    const glm::vec3 baseRight = glm::vec3(data.view[0][0], data.view[1][0], data.view[2][0]);
    const glm::vec3 baseUp = glm::vec3(data.view[0][1], data.view[1][1], data.view[2][1]);

    for (const auto *billboard : m_renderData.billboards)
    {
        if (!billboard || !billboard->isEnabled() || !billboard->getTexture())
            continue;

        VkDescriptorSet ds = getTextureDescriptorSet(
            billboard->getTexture()->vkImageView(), m_linearSampler);

        vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_billboardPipelineLayout, 0, 1, &ds, 0, nullptr);

        BillboardPC pc{};
        pc.viewProj = viewProj;
        const float rotationRadians = glm::radians(billboard->getRotation());
        const float cosAngle = std::cos(rotationRadians);
        const float sinAngle = std::sin(rotationRadians);

        pc.right = baseRight * cosAngle + baseUp * sinAngle;
        pc.size = billboard->getSize();
        pc.up = -baseRight * sinAngle + baseUp * cosAngle;
        pc.pad0 = 0.0f;
        pc.worldPos = billboard->getWorldPosition();
        pc.pad1 = 0;
        pc.color = billboard->getColor();

        vkCmdPushConstants(commandBuffer->vk(), m_billboardPipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(BillboardPC), &pc);

        profiling::cmdDraw(commandBuffer, 6, 1, 0, 0);
    }
}

void UIRenderGraphPass::recordUIText(core::CommandBuffer::SharedPtr commandBuffer, uint32_t currentFrame)
{
    if (m_renderData.texts.empty())
        return;

    GraphicsPipelineKey key{};
    key.shader = ShaderId::UIText;
    key.blend = BlendMode::AlphaBlend;
    key.cull = CullMode::None;
    key.depthTest = false;
    key.depthWrite = false;
    key.colorFormats = {m_format};
    key.pipelineLayout = m_textPipelineLayout;

    auto pipeline = GraphicsPipelineManager::getOrCreate(key);
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    const float aspectCorrection = static_cast<float>(m_extent.height) / static_cast<float>(m_extent.width);

    for (const auto *textObj : m_renderData.texts)
    {
        if (!textObj || !textObj->isEnabled() || textObj->getText().empty())
            continue;

        const ui::Font *font = textObj->getFont();
        if (!font)
            continue;

        ui::FontAtlas *atlas = getOrBuildAtlas(font);
        if (!atlas || !atlas->isBuilt())
            continue;

        VkDescriptorSet ds = getTextureDescriptorSet(
            atlas->getTexture()->vkImageView(), m_nearestSampler);

        vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_textPipelineLayout, 0, 1, &ds, 0, nullptr);

        UITextPC pc{};
        pc.color = textObj->getColor();
        vkCmdPushConstants(commandBuffer->vk(), m_textPipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(UITextPC), &pc);

        std::vector<vertex::Vertex2D> verts;
        const float scale = textObj->getScale() * 0.015f;
        const glm::vec2 pivot = textObj->getPosition();
        glm::vec2 pen = pivot;

        for (char c : textObj->getText())
        {
            const ui::Glyph *g = font->getGlyph(c);
            if (!g)
                continue;

            const auto uvRect = atlas->getGlyphUV(c);
            const float w = g->bitmapWidth * scale * aspectCorrection;
            const float h = g->bitmapRows * scale;
            const float xoff = g->bearing.x * scale * aspectCorrection;
            const float yoff = (g->bearing.y - g->bitmapRows) * scale;

            const float x0 = pen.x + xoff;
            const float x1 = x0 + w;
            const float y0 = pen.y + yoff;
            const float y1 = y0 + h;

            const glm::vec2 p0 = rotatePointAroundPivot(glm::vec2(x0, y0), pivot, textObj->getRotation());
            const glm::vec2 p1 = rotatePointAroundPivot(glm::vec2(x1, y0), pivot, textObj->getRotation());
            const glm::vec2 p2 = rotatePointAroundPivot(glm::vec2(x0, y1), pivot, textObj->getRotation());
            const glm::vec2 p3 = rotatePointAroundPivot(glm::vec2(x1, y1), pivot, textObj->getRotation());

            appendTexturedQuad(verts, p0, p1, p2, p3, uvRect);

            pen.x += (g->advance >> 6) * scale * aspectCorrection;
        }

        if (verts.empty())
            continue;

        auto vb = uploadVertices(verts, currentFrame);
        VkBuffer vkBuf = vb->vk();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, &vkBuf, &offset);
        profiling::cmdDraw(commandBuffer, static_cast<uint32_t>(verts.size()), 1, 0, 0);
    }
}

void UIRenderGraphPass::recordUIButtons(core::CommandBuffer::SharedPtr commandBuffer, uint32_t currentFrame)
{
    if (m_renderData.buttons.empty())
        return;

    GraphicsPipelineKey quadKey{};
    quadKey.shader = ShaderId::UIQuad;
    quadKey.blend = BlendMode::AlphaBlend;
    quadKey.cull = CullMode::None;
    quadKey.depthTest = false;
    quadKey.depthWrite = false;
    quadKey.colorFormats = {m_format};
    quadKey.pipelineLayout = m_quadPipelineLayout;

    auto quadPipeline = GraphicsPipelineManager::getOrCreate(quadKey);

    const float aspectCorrection = static_cast<float>(m_extent.height) / static_cast<float>(m_extent.width);

    for (const auto *btn : m_renderData.buttons)
    {
        if (!btn || !btn->isEnabled())
            continue;

        const glm::vec2 pos = btn->getPosition();
        const glm::vec2 size = btn->getSize();
        const float x0 = pos.x, x1 = pos.x + size.x;
        const float y0 = pos.y, y1 = pos.y + size.y;
        const glm::vec2 pivot = pos + size * 0.5f;

        const glm::vec2 p0 = rotatePointAroundPivot(glm::vec2(x0, y0), pivot, btn->getRotation());
        const glm::vec2 p1 = rotatePointAroundPivot(glm::vec2(x1, y0), pivot, btn->getRotation());
        const glm::vec2 p2 = rotatePointAroundPivot(glm::vec2(x0, y1), pivot, btn->getRotation());
        const glm::vec2 p3 = rotatePointAroundPivot(glm::vec2(x1, y1), pivot, btn->getRotation());

        std::vector<vertex::Vertex2D> quadVerts = {
            {{p0.x, p0.y, 0.f}, {0.f, 0.f}},
            {{p1.x, p1.y, 0.f}, {1.f, 0.f}},
            {{p2.x, p2.y, 0.f}, {0.f, 1.f}},
            {{p1.x, p1.y, 0.f}, {1.f, 0.f}},
            {{p3.x, p3.y, 0.f}, {1.f, 1.f}},
            {{p2.x, p2.y, 0.f}, {0.f, 1.f}}};

        vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, quadPipeline);

        UIQuadPC pc{};
        pc.color = btn->isHovered() ? btn->getHoverColor() : btn->getBackgroundColor();
        pc.borderColor = btn->getBorderColor();
        pc.borderWidth = btn->getBorderWidth();
        pc.cornerRadius = 0.0f;

        vkCmdPushConstants(commandBuffer->vk(), m_quadPipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(UIQuadPC), &pc);

        auto vb = uploadVertices(quadVerts, currentFrame);
        VkBuffer vkBuf = vb->vk();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, &vkBuf, &offset);
        profiling::cmdDraw(commandBuffer, 6, 1, 0, 0);

        // Draw label
        const std::string &label = btn->getLabel();
        if (label.empty())
            continue;
        const ui::Font *font = btn->getFont();
        if (!font)
            continue;
        ui::FontAtlas *atlas = getOrBuildAtlas(font);
        if (!atlas || !atlas->isBuilt())
            continue;

        GraphicsPipelineKey textKey = quadKey;
        textKey.shader = ShaderId::UIText;
        textKey.pipelineLayout = m_textPipelineLayout;
        auto textPipeline = GraphicsPipelineManager::getOrCreate(textKey);
        vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, textPipeline);

        VkDescriptorSet ds = getTextureDescriptorSet(atlas->getTexture()->vkImageView(), m_nearestSampler);
        vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_textPipelineLayout, 0, 1, &ds, 0, nullptr);

        UITextPC textPc{};
        textPc.color = btn->getLabelColor();
        vkCmdPushConstants(commandBuffer->vk(), m_textPipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(UITextPC), &textPc);

        const float scale = btn->getLabelScale() * 0.012f;
        glm::vec2 textSize = font->calculateTextSize(label, scale * aspectCorrection);
        glm::vec2 pen{pos.x + (size.x - textSize.x) * 0.5f,
                      pos.y + (size.y - textSize.y) * 0.5f};

        std::vector<vertex::Vertex2D> textVerts;
        for (char c : label)
        {
            const ui::Glyph *g = font->getGlyph(c);
            if (!g)
                continue;
            const auto uvRect = atlas->getGlyphUV(c);
            const float w = g->bitmapWidth * scale * aspectCorrection;
            const float h = g->bitmapRows * scale;
            const float xoff = g->bearing.x * scale * aspectCorrection;
            const float yoff = (g->bearing.y - g->bitmapRows) * scale;
            const float cx0 = pen.x + xoff, cx1 = cx0 + w;
            const float cy0 = pen.y + yoff, cy1 = cy0 + h;

            const glm::vec2 tp0 = rotatePointAroundPivot(glm::vec2(cx0, cy0), pivot, btn->getRotation());
            const glm::vec2 tp1 = rotatePointAroundPivot(glm::vec2(cx1, cy0), pivot, btn->getRotation());
            const glm::vec2 tp2 = rotatePointAroundPivot(glm::vec2(cx0, cy1), pivot, btn->getRotation());
            const glm::vec2 tp3 = rotatePointAroundPivot(glm::vec2(cx1, cy1), pivot, btn->getRotation());

            appendTexturedQuad(textVerts, tp0, tp1, tp2, tp3, uvRect);

            pen.x += (g->advance >> 6) * scale * aspectCorrection;
        }

        if (!textVerts.empty())
        {
            auto tvb = uploadVertices(textVerts, currentFrame);
            VkBuffer tvkBuf = tvb->vk();
            VkDeviceSize toffset = 0;
            vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, &tvkBuf, &toffset);
            profiling::cmdDraw(commandBuffer, static_cast<uint32_t>(textVerts.size()), 1, 0, 0);
        }
    }
}

void UIRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                               const RenderGraphPassPerFrameData &data,
                               const RenderGraphPassContext &renderContext)
{
    if (renderContext.currentFrame >= m_transientVertexBuffersByFrame.size())
        m_transientVertexBuffersByFrame.resize(renderContext.currentFrame + 1u);

    m_transientVertexBuffersByFrame[renderContext.currentFrame].clear();

    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    recordPassthrough(commandBuffer, renderContext.currentImageIndex);
    recordBillboards(commandBuffer, data);
    recordUIText(commandBuffer, renderContext.currentFrame);
    recordUIButtons(commandBuffer, renderContext.currentFrame);
}

std::vector<IRenderGraphPass::RenderPassExecution>
UIRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution exec{};
    exec.renderArea.offset = {0, 0};
    exec.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_outputRenderTargets[renderContext.currentImageIndex]->vkImageView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = m_clearValues[0];

    exec.colorsRenderingItems = {color};
    exec.useDepth = false;
    exec.colorFormats = {m_format};
    exec.depthFormat = VK_FORMAT_UNDEFINED;

    exec.targets[m_outputHandlers[renderContext.currentImageIndex]] =
        m_outputRenderTargets[renderContext.currentImageIndex];

    return {exec};
}

void UIRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = {0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    m_scissor = {{0, 0}, extent};
    requestRecompilation();
}

void UIRenderGraphPass::setRenderData(const ui::UIRenderData &data)
{
    m_renderData = data;
}

std::vector<VkImageView> UIRenderGraphPass::getOutputImageViews() const
{
    std::vector<VkImageView> imageViews;
    imageViews.reserve(m_outputRenderTargets.size());

    for (const auto *target : m_outputRenderTargets)
    {
        if (!target)
            continue;

        imageViews.push_back(target->vkImageView());
    }

    return imageViews;
}

ui::FontAtlas *UIRenderGraphPass::getOrBuildAtlas(const ui::Font *font)
{
    if (!font)
        return nullptr;

    const std::string key = font->getFontPath().empty() ? "__default__" : font->getFontPath();

    auto it = m_fontAtlases.find(key);
    if (it != m_fontAtlases.end())
        return it->second.get();

    auto atlas = std::make_unique<ui::FontAtlas>();
    if (!atlas->build(*font))
    {
        VX_ENGINE_ERROR_STREAM("UIRenderGraphPass: failed to build font atlas for: " << key);
        return nullptr;
    }

    ui::FontAtlas *raw = atlas.get();
    m_fontAtlases[key] = std::move(atlas);
    return raw;
}

core::Buffer::SharedPtr UIRenderGraphPass::uploadVertices(const std::vector<vertex::Vertex2D> &verts, uint32_t currentFrame)
{
    const VkDeviceSize size = verts.size() * sizeof(vertex::Vertex2D);
    auto buf = core::Buffer::createShared(size,
                                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                          core::memory::MemoryUsage::CPU_TO_GPU);
    buf->upload(verts.data(), size);

    if (currentFrame >= m_transientVertexBuffersByFrame.size())
        m_transientVertexBuffersByFrame.resize(currentFrame + 1u);

    m_transientVertexBuffersByFrame[currentFrame].push_back(buf);
    return buf;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
