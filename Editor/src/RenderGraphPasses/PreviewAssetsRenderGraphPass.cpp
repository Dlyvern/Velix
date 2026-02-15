#include "Editor/RenderGraphPasses/PreviewAssetsRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Builders/GraphicsPipelineBuilder.hpp"
#include "Engine/Builders/RenderPassBuilder.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Primitives.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"

#include <glm/glm.hpp>

struct ModelOnly
{
    glm::mat4 model{1.0f};
    uint32_t objectId{0};
    uint32_t padding[3];
};

ELIX_NESTED_NAMESPACE_BEGIN(editor)

PreviewAssetsRenderGraphPass::PreviewAssetsRenderGraphPass(VkExtent2D extent) : m_extent(extent)
{
    m_viewport = VkViewport{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    m_scissor = VkRect2D{VkOffset2D{0, 0}, m_extent};

    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};

    std::vector<engine::vertex::Vertex3D> vertices;
    std::vector<uint32_t> indices;
    engine::circle::genereteVerticesAndIndices(vertices, indices);

    engine::CPUMesh mesh = engine::CPUMesh::build<engine::vertex::Vertex3D>(vertices, indices);
    m_circleGpuMesh = engine::GPUMesh::createFromMesh(mesh);

    this->setDebugName("Preview assets render graph pass");
}

int PreviewAssetsRenderGraphPass::addMaterialPreviewJob(const PreviewJob &previewJob)
{
    uint32_t index = m_indexBusyJobs++;
    m_renderJobs[index] = previewJob;
    return index;
}

void PreviewAssetsRenderGraphPass::setup(engine::renderGraph::RGPResourcesBuilder &builder)
{
    const auto format = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();
    const auto device = core::VulkanContext::getContext()->getDevice();

    engine::renderGraph::RGPTextureDescription colorTextureDescription(format, engine::renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
    colorTextureDescription.setExtent(m_extent);

    for (int index = 0; index < MAX_RENDER_JOBS; ++index)
    {
        colorTextureDescription.setDebugName("ELIX_MATERIAL_PREVIEW_COLOR_IMAGE_" + std::to_string(index) + "__");

        m_resourceHandlers[index] = builder.createTexture(colorTextureDescription);
    }
}

void PreviewAssetsRenderGraphPass::compile(engine::renderGraph::RGPResourcesStorage &storage)
{
    for (int index = 0; index < MAX_RENDER_JOBS; ++index)
        m_renderTargets[index] = storage.getTexture(m_resourceHandlers[index]);
}

void PreviewAssetsRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassPerFrameData &data,
                                          const engine::RenderGraphPassContext &renderContext)
{
    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    engine::GraphicsPipelineKey key{};
    key.shader = engine::ShaderId::PreviewMesh;
    key.cull = engine::CullMode::Back;
    key.depthTest = true;
    key.depthWrite = true;
    key.depthCompare = VK_COMPARE_OP_LESS;
    key.polygonMode = VK_POLYGON_MODE_FILL;
    key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.colorFormats = {core::VulkanContext::getContext()->getSwapchain()->getImageFormat()};
    key.depthFormat = VK_FORMAT_UNDEFINED;

    auto graphicsPipeline = engine::GraphicsPipelineManager::getOrCreate(key);
    auto pipelineLayout = engine::EngineShaderFamilies::meshShaderFamily.pipelineLayout;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    VkBuffer vertexBuffers[] = {m_circleGpuMesh->vertexBuffer};
    VkDeviceSize offset[] = {0};

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offset);
    vkCmdBindIndexBuffer(commandBuffer, m_circleGpuMesh->indexBuffer, 0, m_circleGpuMesh->indexType);

    static float angle = 0.0f;
    angle += 0.01f;

    glm::mat4 model = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));

    ModelOnly modelPushConstant{
        .model = model};

    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ModelOnly), &modelPushConstant);

    VkDescriptorSet descriptorSet{VK_NULL_HANDLE};

    if (m_renderJobs.size() < m_currentJob)
    {
        std::cerr << "Failed to get job\n";
        descriptorSet = engine::Material::getDefaultMaterial()->getDescriptorSet(renderContext.currentFrame);
    }
    else
        descriptorSet = m_renderJobs.at(m_currentJob).material->getDescriptorSet(renderContext.currentFrame);

    std::vector<VkDescriptorSet> descriptorSets = {
        data.previewCameraDescriptorSet,
        descriptorSet};

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, static_cast<uint32_t>(descriptorSets.size()),
                            descriptorSets.data(), 0, nullptr);

    vkCmdDrawIndexed(commandBuffer, m_circleGpuMesh->indicesCount, 1, 0, 0, 0);

    ++m_currentJob;
}

void PreviewAssetsRenderGraphPass::clearJobs()
{
    m_indexBusyJobs = 0;
    m_currentJob = 0;
}

void PreviewAssetsRenderGraphPass::endBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassContext &context)
{
    for (const auto &colorTarget : m_renderTargets)
    {
        colorTarget->getImage()->insertImageMemoryBarrier(
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, *commandBuffer);
    }
}

void PreviewAssetsRenderGraphPass::startBeginRenderPass(core::CommandBuffer::SharedPtr commandBuffer, const engine::RenderGraphPassContext &context)
{
    for (const auto &colorTarget : m_renderTargets)
    {
        colorTarget->getImage()->insertImageMemoryBarrier(
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, *commandBuffer);
    }
}

std::vector<engine::renderGraph::IRenderGraphPass::RenderPassExecution> PreviewAssetsRenderGraphPass::getRenderPassExecutions(const engine::RenderGraphPassContext &renderContext) const
{
    if (m_indexBusyJobs == 0)
        return {};

    std::vector<IRenderGraphPass::RenderPassExecution> result;
    result.reserve(m_indexBusyJobs);

    for (uint32_t i = 0; i < m_indexBusyJobs; ++i)
    {
        IRenderGraphPass::RenderPassExecution renderPassExecution;
        renderPassExecution.renderArea.offset = {0, 0};
        renderPassExecution.renderArea.extent = m_extent;

        VkRenderingAttachmentInfo color0{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color0.imageView = m_renderTargets[i]->vkImageView();
        color0.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color0.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color0.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color0.clearValue = m_clearValues[0];

        renderPassExecution.colorsRenderingItems = {color0};
        renderPassExecution.useDepth = false;

        renderPassExecution.colorFormats = {core::VulkanContext::getContext()->getSwapchain()->getImageFormat()};
        renderPassExecution.depthFormat = VK_FORMAT_UNDEFINED;

        result.push_back(renderPassExecution);
    }

    return result;
}
ELIX_NESTED_NAMESPACE_END