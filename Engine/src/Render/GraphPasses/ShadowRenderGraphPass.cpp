#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"

#include "Core/VulkanHelpers.hpp"

#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"

#include "Engine/Utilities/ImageUtilities.hpp"
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

    m_pipelineLayout = core::PipelineLayout::createShared(device, std::vector<core::DescriptorSetLayout::SharedPtr>{EngineShaderFamilies::objectDescriptorSetLayout},
                                                          std::vector<VkPushConstantRange>{PushConstant<LightSpaceMatrixPushConstant>::getRange(VK_SHADER_STAGE_VERTEX_BIT)});

    RGPTextureDescription depthTextureDescription(m_depthFormat, RGPTextureUsage::DEPTH_STENCIL);
    depthTextureDescription.setDebugName("__ELIX_SHADOW_DEPTH_TEXTURE__");
    depthTextureDescription.setExtent(m_extent);
    depthTextureDescription.setInitialLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    depthTextureDescription.setFinalLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    builder.createTexture(depthTextureDescription, m_depthTextureHandler);
    builder.write(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
}

void ShadowRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    m_renderTarget = storage.getTexture(m_depthTextureHandler);
}

void ShadowRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                                   const RenderGraphPassContext &renderContext)
{
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
                .lightSpaceMatrix = data.lightSpaceMatrix,
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
    IRenderGraphPass::RenderPassExecution renderPassExecution;
    renderPassExecution.renderArea.offset = {0, 0};
    renderPassExecution.renderArea.extent = m_extent;

    VkRenderingAttachmentInfo depthAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAtt.imageView = m_renderTarget->vkImageView();
    depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAtt.clearValue = m_clearValue;

    renderPassExecution.colorsRenderingItems = {};
    renderPassExecution.depthRenderingItem = depthAtt;

    renderPassExecution.colorFormats = {};
    renderPassExecution.depthFormat = m_depthFormat;

    renderPassExecution.targets[m_depthTextureHandler] = m_renderTarget;

    return {renderPassExecution};
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
