#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"

#include "Core/VulkanHelpers.hpp"

#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Builders/GraphicsPipelineBuilder.hpp"
#include "Engine/Builders/RenderPassBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"

#include <glm/mat4x4.hpp>

struct LightSpaceMatrixPushConstant
{
    glm::mat4 lightSpaceMatrix;
    glm::mat4 model;
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
    const VkFormat depthFormat = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());
    const auto device = core::VulkanContext::getContext()->getDevice();

    m_renderPass = builders::RenderPassBuilder::begin()
                       .addDepthAttachment(depthFormat, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                                           VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
                       .addSubpassDependency(VK_DEPENDENCY_BY_REGION_BIT)
                       .build();

    m_pipelineLayout = core::PipelineLayout::createShared(device, std::vector<core::DescriptorSetLayout::SharedPtr>{},
                                                          std::vector<VkPushConstantRange>{PushConstant<LightSpaceMatrixPushConstant>::getRange(VK_SHADER_STAGE_VERTEX_BIT)});

    RGPTextureDescription depthTextureDescription(depthFormat, RGPTextureUsage::DEPTH_STENCIL);
    depthTextureDescription.setDebugName("__ELIX_SHADOW_DEPTH_TEXTURE__");
    depthTextureDescription.setExtent(m_extent);

    builder.createTexture(depthTextureDescription, m_depthTextureHandler);
    builder.write(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);

    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                                            VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE, VK_COMPARE_OP_ALWAYS, VK_SAMPLER_MIPMAP_MODE_LINEAR);
}

void ShadowRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    m_renderTarget = storage.getTexture(m_depthTextureHandler);

    m_renderTarget->getImage()->insertImageMemoryBarrier(
        0,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1});

    std::vector<VkImageView> attachments{m_renderTarget->vkImageView()};

    m_framebuffer = core::Framebuffer::createShared(core::VulkanContext::getContext()->getDevice(), attachments,
                                                    m_renderPass, m_extent);
}

void ShadowRenderGraphPass::endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer)
{
    m_renderTarget->getImage()->insertImageMemoryBarrier(
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}, *commandBuffer.get());
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
        key.renderPass = m_renderPass;
        key.shader = ShaderId::StaticShadow;
        key.cull = CullMode::None;
        key.depthTest = true;
        key.depthWrite = true;
        key.depthCompare = VK_COMPARE_OP_LESS;
        key.polygonMode = VK_POLYGON_MODE_FILL;
        key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        key.pipelineLayout = m_pipelineLayout;
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
                .model = drawItem.transform};

            vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(LightSpaceMatrixPushConstant),
                               &lightSpaceMatrixPushConstant);
            vkCmdDrawIndexed(commandBuffer, mesh->indicesCount, 1, 0, 0, 0);
        }
    }
}

std::vector<IRenderGraphPass::RenderPassExecution> ShadowRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    IRenderGraphPass::RenderPassExecution renderPassExecution;
    renderPassExecution.clearValues = {m_clearValue};
    renderPassExecution.framebuffer = m_framebuffer;
    renderPassExecution.renderArea.offset = {0, 0};
    renderPassExecution.renderArea.extent = m_extent;
    renderPassExecution.renderPass = m_renderPass;
    return {renderPassExecution};
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END