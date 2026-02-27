#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"

#include <glm/mat4x4.hpp>

struct LightSpaceMatrixPushConstant
{
    glm::mat4 lightSpaceMatrix;
    glm::mat4 model;
    uint32_t bonesOffset{0};
    uint32_t padding[3];
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

ShadowRenderGraphPass::ShadowRenderGraphPass()
{
    this->setDebugName("Shadow render graph pass");
    m_clearValue.depthStencil = {1.0f, 0};
    m_viewport = VkViewport{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    m_scissor.extent = m_extent;
    m_scissor.offset = {0, 0};
}

void ShadowRenderGraphPass::setup(RGPResourcesBuilder &builder)
{
    m_depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());
    const auto device = core::VulkanContext::getContext()->getDevice();

    m_pipelineLayout = core::PipelineLayout::createShared(device,
                                                          std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*EngineShaderFamilies::objectDescriptorSetLayout},
                                                          std::vector<VkPushConstantRange>{PushConstant<LightSpaceMatrixPushConstant>::getRange(VK_SHADER_STAGE_VERTEX_BIT)});

    RGPTextureDescription depthTextureDescription(m_depthFormat, RGPTextureUsage::DEPTH_STENCIL);
    depthTextureDescription.setDebugName("__ELIX_SHADOW_DEPTH_TEXTURE__");
    depthTextureDescription.setExtent(m_extent);
    depthTextureDescription.setInitialLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    depthTextureDescription.setFinalLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    depthTextureDescription.setArrayLayers(ShadowConstants::MAX_DIRECTIONAL_CASCADES);
    depthTextureDescription.setImageViewtype(VK_IMAGE_VIEW_TYPE_2D_ARRAY);

    RGPTextureDescription cubeDepthTextureDescription(m_depthFormat, RGPTextureUsage::DEPTH_STENCIL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    cubeDepthTextureDescription.setExtent(m_extent);
    cubeDepthTextureDescription.setDebugName("__ELIX_CUBE_SHADOW_DEPTH_TEXTURE__");
    cubeDepthTextureDescription.setArrayLayers(ShadowConstants::MAX_POINT_SHADOWS * ShadowConstants::POINT_SHADOW_FACES);
    cubeDepthTextureDescription.setFlags(VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
    cubeDepthTextureDescription.setImageViewtype(VK_IMAGE_VIEW_TYPE_CUBE_ARRAY);

    RGPTextureDescription arrayDepthTextureDescription(m_depthFormat, RGPTextureUsage::DEPTH_STENCIL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    arrayDepthTextureDescription.setExtent(m_extent);
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
    m_executionInfos.reserve(ShadowConstants::MAX_DIRECTIONAL_CASCADES +
                             ShadowConstants::MAX_SPOT_SHADOWS +
                             ShadowConstants::MAX_POINT_SHADOWS * ShadowConstants::POINT_SHADOW_FACES);

    for (uint32_t cascadeIndex = 0; cascadeIndex < ShadowConstants::MAX_DIRECTIONAL_CASCADES; ++cascadeIndex)
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

    m_directionalLayerViews.reserve(ShadowConstants::MAX_DIRECTIONAL_CASCADES);
    for (uint32_t cascadeIndex = 0; cascadeIndex < ShadowConstants::MAX_DIRECTIONAL_CASCADES; ++cascadeIndex)
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
    (void)renderContext;

    if (m_currentExecutionIndex >= m_executionInfos.size())
        return;

    const ShadowExecutionInfo executionInfo = m_executionInfos[m_currentExecutionIndex++];

    glm::mat4 activeLightSpaceMatrix{1.0f};
    bool shouldRender = true;

    switch (executionInfo.type)
    {
    case ShadowExecutionType::Directional:
        if (executionInfo.lightIndex >= data.activeDirectionalCascadeCount)
            shouldRender = false;
        else
            activeLightSpaceMatrix = data.directionalLightSpaceMatrices[executionInfo.lightIndex];
        break;
    case ShadowExecutionType::Spot:
        if (executionInfo.lightIndex >= data.activeSpotShadowCount)
            shouldRender = false;
        else
            activeLightSpaceMatrix = data.spotLightSpaceMatrices[executionInfo.lightIndex];
        break;
    case ShadowExecutionType::Point:
        if (executionInfo.lightIndex >= data.activePointShadowCount)
            shouldRender = false;
        else
        {
            const uint32_t matrixIndex = executionInfo.lightIndex * ShadowConstants::POINT_SHADOW_FACES + executionInfo.faceIndex;
            activeLightSpaceMatrix = data.pointLightSpaceMatrices[matrixIndex];
        }
        break;
    }

    if (!shouldRender)
        return;

    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetDepthBias(commandBuffer, 1.25f, 0.0f, 1.75f);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    for (const auto &[entity, drawItem] : data.drawItems)
    {
        GraphicsPipelineKey key{};
        const bool isSkinned = !drawItem.finalBones.empty();
        key.shader = isSkinned ? ShaderId::SkinnedShadow : ShaderId::StaticShadow;
        key.cull = CullMode::Front;
        key.depthTest = true;
        key.depthWrite = true;
        key.depthCompare = VK_COMPARE_OP_LESS;
        key.polygonMode = VK_POLYGON_MODE_FILL;
        key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        key.pipelineLayout = m_pipelineLayout;
        key.colorFormats = {};
        key.depthFormat = m_depthFormat;

        auto graphicsPipeline = GraphicsPipelineManager::getOrCreate(key);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        for (const auto &mesh : drawItem.meshes)
        {
            VkBuffer vertexBuffers[] = {mesh->vertexBuffer};
            VkDeviceSize offset[] = {0};

            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offset);
            vkCmdBindIndexBuffer(commandBuffer, mesh->indexBuffer, 0, mesh->indexType);

            LightSpaceMatrixPushConstant lightSpaceMatrixPushConstant{
                .lightSpaceMatrix = activeLightSpaceMatrix,
                .model = drawItem.transform,
                .bonesOffset = drawItem.bonesOffset};

            if (isSkinned)
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &data.perObjectDescriptorSet, 0, nullptr);

            vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(LightSpaceMatrixPushConstant),
                               &lightSpaceMatrixPushConstant);
            profiling::cmdDrawIndexed(commandBuffer, mesh->indicesCount, 1, 0, 0, 0);
        }
    }
}

std::vector<IRenderGraphPass::RenderPassExecution> ShadowRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    (void)renderContext;

    m_currentExecutionIndex = 0;

    std::vector<IRenderGraphPass::RenderPassExecution> executions;
    executions.reserve(m_executionInfos.size());

    for (const auto &executionInfo : m_executionInfos)
    {
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
