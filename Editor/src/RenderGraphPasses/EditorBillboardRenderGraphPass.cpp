#include "Editor/RenderGraphPasses/EditorBillboardRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/Logger.hpp"

#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"

#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Components/CameraComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/AudioComponent.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
    enum class GeneratedEditorBillboardIcon : std::size_t
    {
        Camera = 0,
        Light = 1,
        Audio = 2
    };

    std::string toLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    bool hasSuffixInsensitive(const std::string &value, const std::string &suffix)
    {
        if (value.size() < suffix.size())
            return false;

        const std::string valueSuffix = value.substr(value.size() - suffix.size());
        return toLowerCopy(valueSuffix) == toLowerCopy(suffix);
    }

    std::string resolveTextureAssetPath(const std::string &requestedPath)
    {
        if (requestedPath.empty())
            return {};

        const std::filesystem::path normalizedPath = std::filesystem::path(requestedPath).lexically_normal();
        const std::string normalized = normalizedPath.string();
        const std::string normalizedLower = toLowerCopy(normalized);

        if (hasSuffixInsensitive(normalizedLower, ".tex.elixasset"))
            return normalized;

        if (hasSuffixInsensitive(normalizedLower, ".elix.texture"))
        {
            constexpr size_t aliasLength = sizeof(".elix.texture") - 1u;
            const std::string base = normalized.substr(0, normalized.size() - aliasLength);
            return (std::filesystem::path(base + ".tex.elixasset")).lexically_normal().string();
        }

        if (hasSuffixInsensitive(normalizedLower, ".texture.elixasset"))
        {
            constexpr size_t aliasLength = sizeof(".texture.elixasset") - 1u;
            const std::string base = normalized.substr(0, normalized.size() - aliasLength);
            return (std::filesystem::path(base + ".tex.elixasset")).lexically_normal().string();
        }

        if (hasSuffixInsensitive(normalizedLower, ".elixasset"))
            return normalized;

        std::filesystem::path candidateAssetPath = normalizedPath;
        if (candidateAssetPath.has_extension())
            candidateAssetPath.replace_extension(".tex.elixasset");
        else
            candidateAssetPath += ".tex.elixasset";

        return candidateAssetPath.lexically_normal().string();
    }

    elix::engine::Texture::SharedPtr tryLoadIconTexture(const std::string &requestedPath)
    {
        if (requestedPath.empty())
            return nullptr;

        const std::string normalizedRequested = std::filesystem::path(requestedPath).lexically_normal().string();
        const std::string resolvedAssetPath = resolveTextureAssetPath(normalizedRequested);

        if (!resolvedAssetPath.empty())
        {
            auto texture = elix::engine::AssetsLoader::loadTextureGPU(resolvedAssetPath);
            if (texture)
                return texture;
        }

        if (resolvedAssetPath != normalizedRequested)
            return elix::engine::AssetsLoader::loadTextureGPU(normalizedRequested);

        return nullptr;
    }

    float saturate(float value)
    {
        return std::clamp(value, 0.0f, 1.0f);
    }

    void blendIconPixel(std::vector<uint8_t> &rgbaPixels, int size, int x, int y, const glm::vec4 &color)
    {
        if (x < 0 || y < 0 || x >= size || y >= size || color.a <= 0.0f)
            return;

        const std::size_t pixelIndex = static_cast<std::size_t>((y * size + x) * 4);
        const glm::vec4 dstColor(
            rgbaPixels[pixelIndex + 0] / 255.0f,
            rgbaPixels[pixelIndex + 1] / 255.0f,
            rgbaPixels[pixelIndex + 2] / 255.0f,
            rgbaPixels[pixelIndex + 3] / 255.0f);

        const float outAlpha = color.a + dstColor.a * (1.0f - color.a);
        glm::vec3 outRgb(0.0f);
        if (outAlpha > 0.0f)
        {
            outRgb = (glm::vec3(color) * color.a +
                      glm::vec3(dstColor) * dstColor.a * (1.0f - color.a)) /
                     outAlpha;
        }

        rgbaPixels[pixelIndex + 0] = static_cast<uint8_t>(std::round(saturate(outRgb.r) * 255.0f));
        rgbaPixels[pixelIndex + 1] = static_cast<uint8_t>(std::round(saturate(outRgb.g) * 255.0f));
        rgbaPixels[pixelIndex + 2] = static_cast<uint8_t>(std::round(saturate(outRgb.b) * 255.0f));
        rgbaPixels[pixelIndex + 3] = static_cast<uint8_t>(std::round(saturate(outAlpha) * 255.0f));
    }

    float signedDistanceCircle(const glm::vec2 &point, const glm::vec2 &center, float radius)
    {
        return glm::length(point - center) - radius;
    }

    float signedDistanceRoundedBox(const glm::vec2 &point, const glm::vec2 &center, const glm::vec2 &halfExtent, float radius)
    {
        const glm::vec2 q = glm::abs(point - center) - halfExtent + glm::vec2(radius);
        return glm::length(glm::max(q, glm::vec2(0.0f))) +
               std::min(std::max(q.x, q.y), 0.0f) - radius;
    }

    float signedDistanceCapsule(const glm::vec2 &point, const glm::vec2 &a, const glm::vec2 &b, float radius)
    {
        const glm::vec2 segment = b - a;
        const float segmentLengthSquared = glm::dot(segment, segment);
        if (segmentLengthSquared <= 1e-5f)
            return glm::length(point - a) - radius;

        const float t = glm::clamp(glm::dot(point - a, segment) / segmentLengthSquared, 0.0f, 1.0f);
        return glm::length((a + segment * t) - point) - radius;
    }

    template <typename DistanceFn>
    void rasterizeSignedDistanceShape(std::vector<uint8_t> &rgbaPixels,
                                      int size,
                                      DistanceFn &&distanceFn,
                                      const glm::vec4 &color,
                                      float softness = 1.35f)
    {
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                const glm::vec2 point(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                const float distance = distanceFn(point);
                const float coverage = saturate(0.5f - distance / softness);
                if (coverage <= 0.0f)
                    continue;

                glm::vec4 shadedColor = color;
                shadedColor.a *= coverage;
                blendIconPixel(rgbaPixels, size, x, y, shadedColor);
            }
        }
    }

    void rasterizeEditorBillboardBadge(std::vector<uint8_t> &rgbaPixels, int size, const glm::vec4 &ringColor)
    {
        const glm::vec2 center(64.0f, 64.0f);
        rasterizeSignedDistanceShape(
            rgbaPixels, size,
            [center](const glm::vec2 &point)
            { return signedDistanceCircle(point, center, 54.0f); },
            glm::vec4(0.10f, 0.12f, 0.15f, 0.92f),
            1.6f);

        rasterizeSignedDistanceShape(
            rgbaPixels, size,
            [center](const glm::vec2 &point)
            { return signedDistanceCircle(point, center, 50.0f); },
            glm::vec4(0.14f, 0.16f, 0.19f, 0.95f),
            1.4f);

        rasterizeSignedDistanceShape(
            rgbaPixels, size,
            [center](const glm::vec2 &point)
            {
                const float outer = signedDistanceCircle(point, center, 53.0f);
                const float inner = -signedDistanceCircle(point, center, 49.0f);
                return std::max(outer, inner);
            },
            ringColor,
            1.2f);

        rasterizeSignedDistanceShape(
            rgbaPixels, size,
            [center](const glm::vec2 &point)
            { return signedDistanceCapsule(point, center + glm::vec2(-18.0f, -33.0f), center + glm::vec2(18.0f, -33.0f), 2.0f); },
            glm::vec4(1.0f, 1.0f, 1.0f, 0.10f),
            1.0f);
    }

    elix::engine::Texture::SharedPtr createGeneratedEditorBillboardTexture(GeneratedEditorBillboardIcon iconType)
    {
        constexpr int kIconSize = 128;
        std::vector<uint8_t> rgbaPixels(static_cast<std::size_t>(kIconSize) * static_cast<std::size_t>(kIconSize) * 4u, 0u);

        const glm::vec4 glyphColor(0.94f, 0.96f, 0.98f, 1.0f);
        const glm::vec4 cutoutColor(0.12f, 0.15f, 0.18f, 1.0f);

        switch (iconType)
        {
        case GeneratedEditorBillboardIcon::Camera:
        {
            rasterizeEditorBillboardBadge(rgbaPixels, kIconSize, glm::vec4(0.30f, 0.58f, 0.95f, 0.90f));

            rasterizeSignedDistanceShape(
                rgbaPixels, kIconSize,
                [](const glm::vec2 &point)
                { return signedDistanceRoundedBox(point, glm::vec2(64.0f, 68.0f), glm::vec2(24.0f, 15.0f), 7.0f); },
                glyphColor, 1.2f);

            rasterizeSignedDistanceShape(
                rgbaPixels, kIconSize,
                [](const glm::vec2 &point)
                { return signedDistanceRoundedBox(point, glm::vec2(47.0f, 49.0f), glm::vec2(10.0f, 5.0f), 3.0f); },
                glyphColor, 1.0f);

            rasterizeSignedDistanceShape(
                rgbaPixels, kIconSize,
                [](const glm::vec2 &point)
                { return signedDistanceRoundedBox(point, glm::vec2(89.0f, 61.0f), glm::vec2(6.0f, 8.0f), 3.0f); },
                glyphColor, 1.0f);

            rasterizeSignedDistanceShape(
                rgbaPixels, kIconSize,
                [](const glm::vec2 &point)
                { return signedDistanceCircle(point, glm::vec2(64.0f, 68.0f), 10.0f); },
                cutoutColor, 1.0f);

            rasterizeSignedDistanceShape(
                rgbaPixels, kIconSize,
                [](const glm::vec2 &point)
                { return signedDistanceCircle(point, glm::vec2(64.0f, 68.0f), 5.0f); },
                glm::vec4(0.30f, 0.58f, 0.95f, 0.85f), 1.0f);
            break;
        }
        case GeneratedEditorBillboardIcon::Light:
        {
            rasterizeEditorBillboardBadge(rgbaPixels, kIconSize, glm::vec4(0.98f, 0.74f, 0.25f, 0.92f));

            rasterizeSignedDistanceShape(
                rgbaPixels, kIconSize,
                [](const glm::vec2 &point)
                { return signedDistanceCircle(point, glm::vec2(64.0f, 54.0f), 18.0f); },
                glm::vec4(0.98f, 0.91f, 0.78f, 1.0f), 1.2f);

            rasterizeSignedDistanceShape(
                rgbaPixels, kIconSize,
                [](const glm::vec2 &point)
                { return signedDistanceRoundedBox(point, glm::vec2(64.0f, 82.0f), glm::vec2(11.0f, 8.0f), 3.5f); },
                glyphColor, 1.0f);

            rasterizeSignedDistanceShape(
                rgbaPixels, kIconSize,
                [](const glm::vec2 &point)
                {
                    const float outer = signedDistanceCapsule(point, glm::vec2(56.0f, 77.0f), glm::vec2(72.0f, 77.0f), 1.8f);
                    return outer;
                },
                cutoutColor, 0.9f);

            rasterizeSignedDistanceShape(
                rgbaPixels, kIconSize,
                [](const glm::vec2 &point)
                { return signedDistanceCapsule(point, glm::vec2(59.0f, 55.0f), glm::vec2(64.0f, 63.0f), 1.8f); },
                glm::vec4(0.98f, 0.74f, 0.25f, 0.92f), 1.0f);
            rasterizeSignedDistanceShape(
                rgbaPixels, kIconSize,
                [](const glm::vec2 &point)
                { return signedDistanceCapsule(point, glm::vec2(69.0f, 55.0f), glm::vec2(64.0f, 63.0f), 1.8f); },
                glm::vec4(0.98f, 0.74f, 0.25f, 0.92f), 1.0f);
            rasterizeSignedDistanceShape(
                rgbaPixels, kIconSize,
                [](const glm::vec2 &point)
                { return signedDistanceCapsule(point, glm::vec2(64.0f, 63.0f), glm::vec2(64.0f, 69.0f), 1.8f); },
                glm::vec4(0.98f, 0.74f, 0.25f, 0.92f), 1.0f);
            break;
        }
        case GeneratedEditorBillboardIcon::Audio:
        default:
        {
            rasterizeEditorBillboardBadge(rgbaPixels, kIconSize, glm::vec4(0.34f, 0.86f, 0.78f, 0.90f));

            rasterizeSignedDistanceShape(
                rgbaPixels, kIconSize,
                [](const glm::vec2 &point)
                { return signedDistanceCircle(point, glm::vec2(56.0f, 80.0f), 10.5f); },
                glyphColor, 1.2f);

            rasterizeSignedDistanceShape(
                rgbaPixels, kIconSize,
                [](const glm::vec2 &point)
                { return signedDistanceCapsule(point, glm::vec2(66.0f, 44.0f), glm::vec2(66.0f, 80.0f), 4.0f); },
                glyphColor, 1.0f);

            rasterizeSignedDistanceShape(
                rgbaPixels, kIconSize,
                [](const glm::vec2 &point)
                { return signedDistanceCapsule(point, glm::vec2(66.0f, 46.0f), glm::vec2(92.0f, 53.0f), 4.0f); },
                glyphColor, 1.0f);
            break;
        }
        }

        auto texture = std::make_shared<elix::engine::Texture>();
        if (!texture->createFromMemory(rgbaPixels.data(),
                                       rgbaPixels.size(),
                                       static_cast<uint32_t>(kIconSize),
                                       static_cast<uint32_t>(kIconSize),
                                       VK_FORMAT_R8G8B8A8_SRGB,
                                       4u))
            return nullptr;

        return texture;
    }
} // namespace

ELIX_NESTED_NAMESPACE_BEGIN(editor)

EditorBillboardRenderGraphPass::EditorBillboardRenderGraphPass(
    std::shared_ptr<engine::Scene> scene,
    std::vector<engine::renderGraph::RGPResourceHandler> &inputHandlers)
    : m_scene(std::move(scene)), m_inputHandlers(inputHandlers)
{
    setDebugName("Editor billboard render graph pass");
    m_clearValues[0].color = {0.f, 0.f, 0.f, 1.f};
    setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
}

void EditorBillboardRenderGraphPass::setup(engine::renderGraph::RGPResourcesBuilder &builder)
{
    m_format = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();
    m_outputHandlers.clear();

    for (auto &h : m_inputHandlers)
        builder.read(h, engine::renderGraph::RGPTextureUsage::SAMPLED);

    engine::renderGraph::RGPTextureDescription outDesc{m_format, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT};
    outDesc.setInitialLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    outDesc.setFinalLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    outDesc.setCustomExtentFunction([this]
                                    { return m_extent; });

    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        outDesc.setDebugName("__ELIX_EDITOR_BB_" + std::to_string(i) + "__");
        auto h = builder.createTexture(outDesc);
        m_outputHandlers.push_back(h);
        builder.write(h, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
    }

    auto device = core::VulkanContext::getContext()->getDevice();

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_textureDescriptorSetLayout = core::DescriptorSetLayout::createShared(
        device, std::vector<VkDescriptorSetLayoutBinding>{samplerBinding});

    m_pipelineLayout = core::PipelineLayout::createShared(
        device,
        std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_textureDescriptorSetLayout},
        std::vector<VkPushConstantRange>{
            engine::PushConstant<EditorBillboardPC>::getRange(VK_SHADER_STAGE_VERTEX_BIT)});

    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                            VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    m_iconDescriptorSets.fill(VK_NULL_HANDLE);
    for (size_t iconIndex = 0; iconIndex < m_iconTexturePaths.size(); ++iconIndex)
    {
        const std::string resolvedPath = resolveTextureAssetPath(m_iconTexturePaths[iconIndex]);
        if (!m_iconTexturePaths[iconIndex].empty())
            m_iconTextures[iconIndex] = tryLoadIconTexture(m_iconTexturePaths[iconIndex]);

        if (!m_iconTextures[iconIndex])
        {
            m_iconTextures[iconIndex] = createGeneratedEditorBillboardTexture(
                static_cast<GeneratedEditorBillboardIcon>(iconIndex));
        }

        if (!m_iconTextures[iconIndex] && !m_iconTexturePaths[iconIndex].empty())
        {
            VX_EDITOR_WARNING_STREAM("Editor billboard icon texture not found at \"" << m_iconTexturePaths[iconIndex]
                                                                                      << "\" (resolved: \"" << resolvedPath
                                                                                      << "\"). Falling back to generated icon texture.\n");
            m_iconTextures[iconIndex] = createGeneratedEditorBillboardTexture(
                static_cast<GeneratedEditorBillboardIcon>(iconIndex));
        }

        if (!m_iconTextures[iconIndex])
        {
            VX_EDITOR_WARNING_STREAM("Editor billboard pass: failed to create icon texture for icon index "
                                     << iconIndex << ". Falling back to default white texture.\n");
            m_iconTextures[iconIndex] = engine::Texture::getDefaultWhiteTexture();
        }

        if (!m_iconTextures[iconIndex])
            continue;

        m_iconDescriptorSets[iconIndex] = engine::DescriptorSetBuilder::begin()
                                              .addImage(m_iconTextures[iconIndex]->vkImageView(), m_sampler->vk(),
                                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                              .build(device,
                                                     core::VulkanContext::getContext()->getPersistentDescriptorPool(),
                                                     m_textureDescriptorSetLayout);
    }
}

void EditorBillboardRenderGraphPass::compile(engine::renderGraph::RGPResourcesStorage &storage)
{
    const uint32_t imageCount = core::VulkanContext::getContext()->getSwapchain()->getImageCount();

    m_outputRenderTargets.resize(imageCount);
    m_passthroughDescriptorSets.resize(imageCount, VK_NULL_HANDLE);

    auto device = core::VulkanContext::getContext()->getDevice();
    auto pool = core::VulkanContext::getContext()->getPersistentDescriptorPool();

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        m_outputRenderTargets[i] = storage.getTexture(m_outputHandlers[i]);

        const auto *inputTexture = storage.getTexture(m_inputHandlers[i]);
        if (!inputTexture)
        {
            VX_EDITOR_WARNING_STREAM("Editor billboard pass: failed to get input texture for frame " << i << '\n');
            continue;
        }

        if (!m_passthroughSetsBuilt)
        {
            m_passthroughDescriptorSets[i] = engine::DescriptorSetBuilder::begin()
                                                 .addImage(inputTexture->vkImageView(), m_sampler->vk(),
                                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                                                 .build(device, pool, m_textureDescriptorSetLayout);
        }
        else
        {
            engine::DescriptorSetBuilder::begin()
                .addImage(inputTexture->vkImageView(), m_sampler->vk(),
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
                .update(device, m_passthroughDescriptorSets[i]);
        }
    }

    if (!m_passthroughSetsBuilt)
        m_passthroughSetsBuilt = true;
}

void EditorBillboardRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer,
                                            const engine::RenderGraphPassPerFrameData &data,
                                            const engine::RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);

    if (renderContext.currentImageIndex >= m_passthroughDescriptorSets.size())
        return;

    VkDescriptorSet passthroughDescriptorSet = m_passthroughDescriptorSets[renderContext.currentImageIndex];
    if (passthroughDescriptorSet == VK_NULL_HANDLE)
        return;

    // Pass-through current frame input into this pass output.
    engine::GraphicsPipelineKey passthroughKey{};
    passthroughKey.shader = engine::ShaderId::Present;
    passthroughKey.blend = engine::BlendMode::None;
    passthroughKey.cull = engine::CullMode::None;
    passthroughKey.depthTest = false;
    passthroughKey.depthWrite = false;
    passthroughKey.colorFormats = {m_format};
    passthroughKey.pipelineLayout = m_pipelineLayout;

    auto passthroughPipeline = engine::GraphicsPipelineManager::getOrCreate(passthroughKey);
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, passthroughPipeline);
    vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &passthroughDescriptorSet, 0, nullptr);
    engine::renderGraph::profiling::cmdDraw(commandBuffer, 3, 1, 0, 0);

    if (!m_showBillboards || !m_scene)
        return;

    engine::GraphicsPipelineKey billboardKey{};
    billboardKey.shader = engine::ShaderId::Billboard;
    billboardKey.blend = engine::BlendMode::AlphaBlend;
    billboardKey.cull = engine::CullMode::None;
    billboardKey.depthTest = false;
    billboardKey.depthWrite = false;
    billboardKey.colorFormats = {m_format};
    billboardKey.pipelineLayout = m_pipelineLayout;

    auto billboardPipeline = engine::GraphicsPipelineManager::getOrCreate(billboardKey);
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, billboardPipeline);

    const glm::mat4 viewProj = data.projection * data.view;
    const glm::vec3 right = glm::vec3(data.view[0][0], data.view[1][0], data.view[2][0]);
    const glm::vec3 up = glm::vec3(data.view[0][1], data.view[1][1], data.view[2][1]);

    constexpr float kIconSize = 0.4f;

    struct IconDef
    {
        uint32_t type;
        glm::vec4 color;
    };

    for (const auto &entity : m_scene->getEntities())
    {
        if (!entity || !entity->isEnabled())
            continue;

        auto *transform = entity->getComponent<engine::Transform3DComponent>();
        if (!transform)
            continue;

        const glm::vec3 worldPos = transform->getWorldPosition();
        auto *cameraComponent = entity->getComponent<engine::CameraComponent>();

        // Collect all icon types this entity needs
        IconDef icons[3];
        int iconCount = 0;

        if (cameraComponent)
            icons[iconCount++] = {0u, {1.0f, 1.0f, 1.0f, 1.0f}};
        if (entity->getComponent<engine::LightComponent>())
            icons[iconCount++] = {1u, {1.0f, 1.0f, 1.0f, 1.0f}};
        if (entity->getComponent<engine::AudioComponent>())
            icons[iconCount++] = {2u, {1.0f, 1.0f, 1.0f, 1.0f}};

        for (int i = 0; i < iconCount; ++i)
        {
            if (icons[i].type >= m_iconDescriptorSets.size())
                continue;

            VkDescriptorSet iconDescriptorSet = m_iconDescriptorSets[icons[i].type];
            if (iconDescriptorSet == VK_NULL_HANDLE)
                continue;

            vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout, 0, 1, &iconDescriptorSet, 0, nullptr);

            EditorBillboardPC pc{};
            pc.viewProj = viewProj;
            pc.right = right;
            pc.size = kIconSize;
            pc.up = up;
            pc.pad0 = 0.0f;
            glm::vec3 iconWorldPos = worldPos;
            if (icons[i].type == 0u && cameraComponent)
            {
                const auto camera = cameraComponent->getCamera();
                if (camera)
                    iconWorldPos = camera->getPosition();
            }
            pc.worldPos = iconWorldPos;
            pc.pad1 = 0;
            pc.color = icons[i].color;

            vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(EditorBillboardPC), &pc);

            engine::renderGraph::profiling::cmdDraw(commandBuffer, 6, 1, 0, 0);
        }
    }
}

std::vector<engine::renderGraph::IRenderGraphPass::RenderPassExecution>
EditorBillboardRenderGraphPass::getRenderPassExecutions(
    const engine::RenderGraphPassContext &renderContext) const
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

void EditorBillboardRenderGraphPass::setExtent(VkExtent2D extent)
{
    m_extent = extent;
    m_viewport = {0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    m_scissor = {{0, 0}, extent};
    requestRecompilation();
}

void EditorBillboardRenderGraphPass::setScene(std::shared_ptr<engine::Scene> scene)
{
    m_scene = std::move(scene);
}

void EditorBillboardRenderGraphPass::setBillboardsVisible(bool visible)
{
    m_showBillboards = visible;
}

void EditorBillboardRenderGraphPass::setIconTexturePath(std::string texturePath)
{
    if (texturePath.empty())
        return;

    bool changed = false;
    for (auto &path : m_iconTexturePaths)
    {
        if (path == texturePath)
            continue;

        path = texturePath;
        changed = true;
    }

    if (changed)
        requestRecompilation();
}

void EditorBillboardRenderGraphPass::setCameraIconTexturePath(std::string texturePath)
{
    if (texturePath.empty() || m_iconTexturePaths[0] == texturePath)
        return;

    m_iconTexturePaths[0] = std::move(texturePath);
    requestRecompilation();
}

void EditorBillboardRenderGraphPass::setLightIconTexturePath(std::string texturePath)
{
    if (texturePath.empty() || m_iconTexturePaths[1] == texturePath)
        return;

    m_iconTexturePaths[1] = std::move(texturePath);
    requestRecompilation();
}

void EditorBillboardRenderGraphPass::setAudioIconTexturePath(std::string texturePath)
{
    if (texturePath.empty() || m_iconTexturePaths[2] == texturePath)
        return;

    m_iconTexturePaths[2] = std::move(texturePath);
    requestRecompilation();
}

ELIX_NESTED_NAMESPACE_END
