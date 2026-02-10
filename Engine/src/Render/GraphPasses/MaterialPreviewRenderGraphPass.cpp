#include "Engine/Render/GraphPasses/MaterialPreviewRenderGraphPass.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/Shader.hpp"
#include "Core/Cache/GraphicsPipelineCache.hpp"

#include "Engine/Builders/GraphicsPipelineBuilder.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include <glm/glm.hpp>

struct ModelOnly
{
    glm::mat4 model;
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

MaterialPreviewRenderGraphPass::MaterialPreviewRenderGraphPass(VkExtent2D extent) : m_extent(extent)
{
    m_viewport = VkViewport{0.0f, 0.0f, static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), 0.0f, 1.0f};
    m_scissor = VkRect2D{VkOffset2D{0, 0}, m_extent};

    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};

    float radius = 1.0f;
    int sectorCount = 32;
    int stackCount = 16;
    std::vector<engine::vertex::Vertex3D> vertices;
    std::vector<uint32_t> indices;

    float sectorStep = 2 * M_PI / sectorCount;
    float stackStep = M_PI / stackCount;

    for (int i = 0; i <= stackCount; ++i)
    {
        float stackAngle = M_PI / 2 - i * stackStep;
        float xy = radius * cosf(stackAngle);
        float z = radius * sinf(stackAngle);

        for (int j = 0; j <= sectorCount; ++j)
        {
            float sectorAngle = j * sectorStep;

            engine::vertex::Vertex3D vertex;
            vertex.position.x = xy * cosf(sectorAngle);
            vertex.position.y = xy * sinf(sectorAngle);
            vertex.position.z = z;

            vertex.normal = glm::normalize(vertex.position);

            vertex.textureCoordinates.x = (float)j / sectorCount;
            vertex.textureCoordinates.y = (float)i / stackCount;

            vertices.push_back(vertex);
        }
    }
    for (int i = 0; i < stackCount; ++i)
    {
        int k1 = i * (sectorCount + 1);
        int k2 = k1 + sectorCount + 1;

        for (int j = 0; j < sectorCount; ++j, ++k1, ++k2)
        {
            if (i != 0)
            {
                indices.push_back(k1);
                indices.push_back(k2);
                indices.push_back(k1 + 1);
            }

            if (i != (stackCount - 1))
            {
                indices.push_back(k1 + 1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            }
        }
    }

    engine::CPUMesh mesh = engine::CPUMesh::build<engine::vertex::Vertex3D>(vertices, indices);
    m_gpuMesh = GPUMesh::createFromMesh(mesh);
}

void MaterialPreviewRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    const auto &swapChain = core::VulkanContext::getContext()->getSwapchain();
    auto format = swapChain->getImageFormat();

    RGPTextureDescription colorTextureDescription(format, RGPTextureUsage::COLOR_ATTACHMENT);
    colorTextureDescription.setDebugName("ELIX_MATERIAL_PREVIEW_COLOR_IMAGE");
    colorTextureDescription.setExtent(m_extent);

    m_colorTextureHandler = builder.createTexture(colorTextureDescription);

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::vector<VkAttachmentReference> attachmentReferences(1);
    attachmentReferences[0].attachment = 0;
    attachmentReferences[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = 0;

    VkSubpassDescription des{};
    des.colorAttachmentCount = static_cast<uint32_t>(attachmentReferences.size());
    des.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    des.pColorAttachments = attachmentReferences.data();
    des.pDepthStencilAttachment = nullptr;
    des.flags = 0;

    m_renderPass = core::RenderPass::createShared(
        std::vector<VkAttachmentDescription>{colorAttachment},
        std::vector<VkSubpassDescription>{des},
        std::vector<VkSubpassDependency>{dependency});

    core::Shader shader("./resources/shaders/shader_simple_textured_mesh.vert.spv",
                        "./resources/shaders/shader_simple_textured_mesh.frag.spv");

    //? what is this parameter
    const VkViewport viewport = swapChain->getViewport();
    const VkRect2D scissor = swapChain->getScissor();
    std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::Vertex3D))};
    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions = vertex::Vertex3D::getAttributeDescriptions();

    const std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const auto device = core::VulkanContext::getContext()->getDevice();
    const auto cache = core::cache::GraphicsPipelineCache::getDeviceCache(device);

    auto inputAssembly = builders::GraphicsPipelineBuilder::inputAssemblyCI(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    auto rasterizer = builders::GraphicsPipelineBuilder::rasterizationCI(VK_POLYGON_MODE_FILL);
    auto multisampling = builders::GraphicsPipelineBuilder::multisamplingCI();
    auto depthStencil = builders::GraphicsPipelineBuilder::depthStencilCI(true, true, VK_COMPARE_OP_LESS);
    auto viewportState = builders::GraphicsPipelineBuilder::viewportCI({viewport}, {scissor});
    auto dynamicState = builders::GraphicsPipelineBuilder::dynamic(dynamicStates);
    auto vertexInputState = builders::GraphicsPipelineBuilder::vertexInputCI(vertexBindingDescriptions, vertexAttributeDescriptions);

    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments{
        builders::GraphicsPipelineBuilder::colorBlendAttachmentCI(false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO)};

    auto colorBlending = builders::GraphicsPipelineBuilder::colorBlending(colorBlendAttachments);

    m_graphicsPipeline = core::GraphicsPipeline::createShared(device, m_renderPass->vk(), shader.getShaderStages(),
                                                              EngineShaderFamilies::texturedStatisMeshShaderFamily.pipelineLayout->vk(), dynamicState, colorBlending, multisampling, rasterizer,
                                                              viewportState, inputAssembly, vertexInputState, 0, depthStencil, cache);
}

void MaterialPreviewRenderGraphPass::compile(RGPResourcesStorage &storage)
{
    m_colorRenderTarget = storage.getTexture(m_colorTextureHandler);

    std::vector<VkImageView> attachments{
        m_colorRenderTarget->vkImageView()};

    m_framebuffer = core::Framebuffer::createShared(core::VulkanContext::getContext()->getDevice(),
                                                    attachments, m_renderPass, m_extent);
}

void MaterialPreviewRenderGraphPass::execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data)
{
    vkCmdSetViewport(commandBuffer, 0, 1, &m_viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &m_scissor);

    core::PipelineLayout::SharedPtr pipelineLayout = EngineShaderFamilies::texturedStatisMeshShaderFamily.pipelineLayout;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

    VkBuffer vertexBuffers[] = {m_gpuMesh->vertexBuffer};
    VkDeviceSize offset[] = {0};

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offset);
    vkCmdBindIndexBuffer(commandBuffer, m_gpuMesh->indexBuffer, 0, m_gpuMesh->indexType);

    static float angle = 0.0f;
    angle += 0.01f;

    glm::mat4 model = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));

    ModelOnly modelPushConstant{
        .model = model};

    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ModelOnly), &modelPushConstant);

    std::vector<VkDescriptorSet> descriptorSets;

    descriptorSets = {
        data.previewCameraDescriptorSet,
        Material::getDefaultMaterial()->getDescriptorSet(m_currentFrame)};

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, static_cast<uint32_t>(descriptorSets.size()),
                            descriptorSets.data(), 0, nullptr);

    vkCmdDrawIndexed(commandBuffer, m_gpuMesh->indicesCount, 1, 0, 0, 0);
}

void MaterialPreviewRenderGraphPass::update(const RenderGraphPassContext &renderData)
{
    m_currentFrame = renderData.currentFrame;
}

void MaterialPreviewRenderGraphPass::getRenderPassBeginInfo(VkRenderPassBeginInfo &renderPassBeginInfo) const
{
    renderPassBeginInfo = VkRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass = m_renderPass->vk();
    renderPassBeginInfo.framebuffer = m_framebuffer->vk();
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = m_extent;
    renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(m_clearValues.size());
    renderPassBeginInfo.pClearValues = m_clearValues.data();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END