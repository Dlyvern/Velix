#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"

#include <algorithm>
#include <glm/mat4x4.hpp>

struct LightSpaceMatrixPushConstant
{
    glm::mat4 lightSpaceMatrix;
    uint32_t baseInstance{0};
    uint32_t padding[3]{0, 0, 0};
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

ShadowRenderGraphPass::ShadowRenderGraphPass()
{
    this->setDebugName("Shadow render graph pass");
    m_clearValue.depthStencil = {1.0f, 0};
    updateDirectionalCascadeCountFromSettings();
    updateShadowExtentFromSettings();
}

void ShadowRenderGraphPass::setShadowExtent(VkExtent2D extent)
{
    const bool extentChanged = (m_extent.width != extent.width) || (m_extent.height != extent.height);
    m_extent = extent;
    m_viewport = VkViewport{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    m_scissor.extent = m_extent;
    m_scissor.offset = {0, 0};

    if (extentChanged)
        requestRecompilation();
}

void ShadowRenderGraphPass::updateShadowExtentFromSettings()
{
    const auto &settings = RenderQualitySettings::getInstance();
    const uint32_t shadowResolution = std::max(settings.getShadowResolution(), 1u);
    setShadowExtent(VkExtent2D{shadowResolution, shadowResolution});
}

void ShadowRenderGraphPass::updateDirectionalCascadeCountFromSettings()
{
    const auto &settings = RenderQualitySettings::getInstance();
    const uint32_t configuredCascadeCount = std::max(1u, std::min(settings.getShadowCascadeCount(), ShadowConstants::MAX_DIRECTIONAL_CASCADES));
    m_directionalCascadeCount = configuredCascadeCount;
}

void ShadowRenderGraphPass::syncQualitySettings()
{
    const uint32_t oldCascadeCount = m_directionalCascadeCount;
    updateDirectionalCascadeCountFromSettings();
    if (oldCascadeCount != m_directionalCascadeCount)
        requestRecompilation();

    updateShadowExtentFromSettings();
}

void ShadowRenderGraphPass::setup(RGPResourcesBuilder &builder)
{
    updateShadowExtentFromSettings();
    m_depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());
    const auto device = core::VulkanContext::getContext()->getDevice();

    m_pipelineLayout = core::PipelineLayout::createShared(device,
                                                          std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*EngineShaderFamilies::objectDescriptorSetLayout},
                                                          std::vector<VkPushConstantRange>{PushConstant<LightSpaceMatrixPushConstant>::getRange(VK_SHADER_STAGE_VERTEX_BIT)});

    RGPTextureDescription depthTextureDescription(m_depthFormat, RGPTextureUsage::DEPTH_STENCIL);
    depthTextureDescription.setDebugName("__ELIX_SHADOW_DEPTH_TEXTURE__");
    depthTextureDescription.setCustomExtentFunction([this]
                                                    { return m_extent; });
    depthTextureDescription.setInitialLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    depthTextureDescription.setFinalLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    depthTextureDescription.setArrayLayers(ShadowConstants::MAX_DIRECTIONAL_CASCADES);
    depthTextureDescription.setImageViewtype(VK_IMAGE_VIEW_TYPE_2D_ARRAY);

    RGPTextureDescription cubeDepthTextureDescription(m_depthFormat, RGPTextureUsage::DEPTH_STENCIL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    cubeDepthTextureDescription.setCustomExtentFunction([this]
                                                        { return m_extent; });
    cubeDepthTextureDescription.setDebugName("__ELIX_CUBE_SHADOW_DEPTH_TEXTURE__");
    cubeDepthTextureDescription.setArrayLayers(ShadowConstants::MAX_POINT_SHADOWS * ShadowConstants::POINT_SHADOW_FACES);
    cubeDepthTextureDescription.setFlags(VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
    cubeDepthTextureDescription.setImageViewtype(VK_IMAGE_VIEW_TYPE_CUBE_ARRAY);

    RGPTextureDescription arrayDepthTextureDescription(m_depthFormat, RGPTextureUsage::DEPTH_STENCIL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    arrayDepthTextureDescription.setCustomExtentFunction([this]
                                                         { return m_extent; });
    arrayDepthTextureDescription.setDebugName("__ELIX_ARRAY_SHADOW_DEPTH_TEXTURE__");
    arrayDepthTextureDescription.setArrayLayers(ShadowConstants::MAX_SPOT_SHADOWS);
    arrayDepthTextureDescription.setImageViewtype(VK_IMAGE_VIEW_TYPE_2D_ARRAY);

    builder.createTexture(depthTextureDescription, m_depthTextureHandler);
    builder.write(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);

    builder.createTexture(cubeDepthTextureDescription, m_depthCubeTextureHandler);
    builder.write(m_depthCubeTextureHandler, RGPTextureUsage::DEPTH_STENCIL);

    builder.createTexture(arrayDepthTextureDescription, m_depthArrayTextureHandler);
    builder.write(m_depthArrayTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
}

void ShadowRenderGraphPass::compile(RGPResourcesStorage &storage)
{
    destroyLayerViews();

    m_renderTarget = storage.getTexture(m_depthTextureHandler);
    m_arrayRenderTarget = storage.getTexture(m_depthArrayTextureHandler);
    m_cubeRenderTarget = storage.getTexture(m_depthCubeTextureHandler);

    rebuildLayerViews();

    m_executionInfos.clear();
    m_executionInfos.reserve(m_directionalCascadeCount +
                             ShadowConstants::MAX_SPOT_SHADOWS +
                             ShadowConstants::MAX_POINT_SHADOWS * ShadowConstants::POINT_SHADOW_FACES);

    for (uint32_t cascadeIndex = 0; cascadeIndex < m_directionalCascadeCount; ++cascadeIndex)
        m_executionInfos.push_back(ShadowExecutionInfo{.type = ShadowExecutionType::Directional, .layer = cascadeIndex, .lightIndex = cascadeIndex, .faceIndex = 0});

    for (uint32_t spotIndex = 0; spotIndex < ShadowConstants::MAX_SPOT_SHADOWS; ++spotIndex)
        m_executionInfos.push_back(ShadowExecutionInfo{.type = ShadowExecutionType::Spot, .layer = spotIndex, .lightIndex = spotIndex, .faceIndex = 0});

    for (uint32_t pointIndex = 0; pointIndex < ShadowConstants::MAX_POINT_SHADOWS; ++pointIndex)
    {
        for (uint32_t face = 0; face < ShadowConstants::POINT_SHADOW_FACES; ++face)
        {
            const uint32_t layer = pointIndex * ShadowConstants::POINT_SHADOW_FACES + face;
            m_executionInfos.push_back(ShadowExecutionInfo{.type = ShadowExecutionType::Point, .layer = layer, .lightIndex = pointIndex, .faceIndex = face});
        }
    }
}

void ShadowRenderGraphPass::cleanup()
{
    destroyLayerViews();
}

VkImageView ShadowRenderGraphPass::createSingleLayerView(const RenderTarget *target, uint32_t baseArrayLayer) const
{
    if (!target || !target->getImage())
        return VK_NULL_HANDLE;

    VkImageView imageView{VK_NULL_HANDLE};
    VkImageViewCreateInfo imageViewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    imageViewCI.image = target->getImage()->vk();
    imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCI.format = m_depthFormat;
    imageViewCI.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    imageViewCI.subresourceRange.baseMipLevel = 0;
    imageViewCI.subresourceRange.levelCount = 1;
    imageViewCI.subresourceRange.baseArrayLayer = baseArrayLayer;
    imageViewCI.subresourceRange.layerCount = 1;

    if (vkCreateImageView(core::VulkanContext::getContext()->getDevice(), &imageViewCI, nullptr, &imageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create single-layer shadow image view");

    return imageView;
}

void ShadowRenderGraphPass::destroyLayerViews()
{
    auto device = core::VulkanContext::getContext()->getDevice();

    for (auto &view : m_spotLayerViews)
    {
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device, view, nullptr);
    }
    m_spotLayerViews.clear();

    for (auto &view : m_pointLayerViews)
    {
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device, view, nullptr);
    }
    m_pointLayerViews.clear();

    for (auto &view : m_directionalLayerViews)
    {
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device, view, nullptr);
    }
    m_directionalLayerViews.clear();
}

void ShadowRenderGraphPass::rebuildLayerViews()
{
    if (!m_renderTarget || !m_arrayRenderTarget || !m_cubeRenderTarget)
        return;

    m_directionalLayerViews.reserve(m_directionalCascadeCount);
    for (uint32_t cascadeIndex = 0; cascadeIndex < m_directionalCascadeCount; ++cascadeIndex)
        m_directionalLayerViews.push_back(createSingleLayerView(m_renderTarget, cascadeIndex));

    m_spotLayerViews.reserve(ShadowConstants::MAX_SPOT_SHADOWS);
    for (uint32_t spotIndex = 0; spotIndex < ShadowConstants::MAX_SPOT_SHADOWS; ++spotIndex)
        m_spotLayerViews.push_back(createSingleLayerView(m_arrayRenderTarget, spotIndex));

    const uint32_t pointLayerCount = ShadowConstants::MAX_POINT_SHADOWS * ShadowConstants::POINT_SHADOW_FACES;
    m_pointLayerViews.reserve(pointLayerCount);
    for (uint32_t layer = 0; layer < pointLayerCount; ++layer)
        m_pointLayerViews.push_back(createSingleLayerView(m_cubeRenderTarget, layer));
}

void ShadowRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                                   const RenderGraphPassContext &renderContext)
{
    if (renderContext.executionIndex >= m_executionInfos.size())
        return;

    const ShadowExecutionInfo executionInfo = m_executionInfos[renderContext.executionIndex];

    glm::mat4 activeLightSpaceMatrix{1.0f};
    const std::vector<DrawBatch> *executionBatches{nullptr};
    bool shouldRender = true;

    switch (executionInfo.type)
    {
    case ShadowExecutionType::Directional:
        if (executionInfo.lightIndex >= data.activeDirectionalCascadeCount)
            shouldRender = false;
        else
        {
            activeLightSpaceMatrix = data.directionalLightSpaceMatrices[executionInfo.lightIndex];
            executionBatches = &data.directionalShadowDrawBatches[executionInfo.lightIndex];
        }
        break;
    case ShadowExecutionType::Spot:
        if (executionInfo.lightIndex >= data.activeSpotShadowCount)
            shouldRender = false;
        else
        {
            activeLightSpaceMatrix = data.spotLightSpaceMatrices[executionInfo.lightIndex];
            executionBatches = &data.spotShadowDrawBatches[executionInfo.lightIndex];
        }
        break;
    case ShadowExecutionType::Point:
        if (executionInfo.lightIndex >= data.activePointShadowCount)
            shouldRender = false;
        else
        {
            const uint32_t matrixIndex = executionInfo.lightIndex * ShadowConstants::POINT_SHADOW_FACES + executionInfo.faceIndex;
            activeLightSpaceMatrix = data.pointLightSpaceMatrices[matrixIndex];
            executionBatches = &data.pointShadowDrawBatches[matrixIndex];
        }
        break;
    }

    if (!shouldRender || !executionBatches || executionBatches->empty())
        return;

    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetDepthBias(commandBuffer, 1.25f, 0.0f, 1.75f);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    const VkDescriptorSet perObjectSet = data.shadowPerObjectDescriptorSet ? data.shadowPerObjectDescriptorSet : data.perObjectDescriptorSet;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &perObjectSet, 0, nullptr);

    // Resolve both pipeline variants once — avoids re-hashing the key per draw.
    auto makeKey = [&](bool skinned) -> GraphicsPipelineKey
    {
        GraphicsPipelineKey k{};
        k.shader = skinned ? ShaderId::SkinnedShadow : ShaderId::StaticShadow;
        k.cull = CullMode::Front;
        k.depthTest = true;
        k.depthWrite = true;
        k.depthCompare = VK_COMPARE_OP_LESS;
        k.polygonMode = VK_POLYGON_MODE_FILL;
        k.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        k.pipelineLayout = m_pipelineLayout;
        k.depthFormat = m_depthFormat;
        return k;
    };
    const VkPipeline staticPipeline  = GraphicsPipelineManager::getOrCreate(makeKey(false));
    const VkPipeline skinnedPipeline = GraphicsPipelineManager::getOrCreate(makeKey(true));

    VkPipeline boundPipeline     = VK_NULL_HANDLE;
    VkBuffer   boundVertexBuffer = VK_NULL_HANDLE;
    VkBuffer   boundIndexBuffer  = VK_NULL_HANDLE;

    // The light-space matrix is constant for the whole cascade — push it once.
    // Per draw we only need to update baseInstance (offset 64, 4 bytes).
    {
        LightSpaceMatrixPushConstant initial{.lightSpaceMatrix = activeLightSpaceMatrix, .baseInstance = 0};
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(LightSpaceMatrixPushConstant), &initial);
    }

    for (const auto &batch : *executionBatches)
    {
        if (!batch.mesh || batch.instanceCount == 0)
            continue;

        const VkPipeline batchPipeline = batch.skinned ? skinnedPipeline : staticPipeline;
        if (batchPipeline != boundPipeline)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batchPipeline);
            boundPipeline = batchPipeline;
        }

        const VkBuffer vb = batch.mesh->vertexBuffer;
        if (vb != boundVertexBuffer)
        {
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, &offset);
            boundVertexBuffer = vb;
        }

        const VkBuffer ib = batch.mesh->indexBuffer;
        if (ib != boundIndexBuffer)
        {
            vkCmdBindIndexBuffer(commandBuffer, ib, 0, batch.mesh->indexType);
            boundIndexBuffer = ib;
        }

        // Only update the per-draw baseInstance (offset 64, 4 bytes).
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           offsetof(LightSpaceMatrixPushConstant, baseInstance), sizeof(uint32_t),
                           &batch.firstInstance);
        profiling::cmdDrawIndexed(commandBuffer, batch.mesh->indicesCount, batch.instanceCount, 0, 0, 0);
    }
}

std::vector<IRenderGraphPass::RenderPassExecution> ShadowRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    const uint32_t activeDirectionalCount = std::min(renderContext.activeDirectionalShadowCount, m_directionalCascadeCount);
    const uint32_t activeSpotCount = std::min(renderContext.activeSpotShadowCount, ShadowConstants::MAX_SPOT_SHADOWS);
    const uint32_t activePointCount = std::min(renderContext.activePointShadowCount, ShadowConstants::MAX_POINT_SHADOWS);
    const uint32_t activeExecutionCount =
        activeDirectionalCount +
        activeSpotCount +
        activePointCount * ShadowConstants::POINT_SHADOW_FACES;

    std::vector<IRenderGraphPass::RenderPassExecution> executions;
    executions.reserve(activeExecutionCount);

    for (const auto &executionInfo : m_executionInfos)
    {
        if (executionInfo.type == ShadowExecutionType::Directional && executionInfo.lightIndex >= activeDirectionalCount)
            continue;
        if (executionInfo.type == ShadowExecutionType::Spot && executionInfo.lightIndex >= activeSpotCount)
            continue;
        if (executionInfo.type == ShadowExecutionType::Point && executionInfo.lightIndex >= activePointCount)
            continue;

        IRenderGraphPass::RenderPassExecution renderPassExecution;
        renderPassExecution.renderArea.offset = {0, 0};
        renderPassExecution.renderArea.extent = m_extent;
        renderPassExecution.colorsRenderingItems = {};
        renderPassExecution.colorFormats = {};
        renderPassExecution.depthFormat = m_depthFormat;

        VkRenderingAttachmentInfo depthAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.clearValue = m_clearValue;

        switch (executionInfo.type)
        {
        case ShadowExecutionType::Directional:
            depthAtt.imageView = executionInfo.layer < m_directionalLayerViews.size() ? m_directionalLayerViews[executionInfo.layer] : VK_NULL_HANDLE;
            if (m_renderTarget)
                renderPassExecution.targets[m_depthTextureHandler] = m_renderTarget;
            break;
        case ShadowExecutionType::Spot:
            depthAtt.imageView = executionInfo.layer < m_spotLayerViews.size() ? m_spotLayerViews[executionInfo.layer] : VK_NULL_HANDLE;
            if (m_arrayRenderTarget)
                renderPassExecution.targets[m_depthArrayTextureHandler] = m_arrayRenderTarget;
            break;
        case ShadowExecutionType::Point:
            depthAtt.imageView = executionInfo.layer < m_pointLayerViews.size() ? m_pointLayerViews[executionInfo.layer] : VK_NULL_HANDLE;
            if (m_cubeRenderTarget)
                renderPassExecution.targets[m_depthCubeTextureHandler] = m_cubeRenderTarget;
            break;
        }

        renderPassExecution.depthRenderingItem = depthAtt;
        executions.push_back(renderPassExecution);
    }

    return executions;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
