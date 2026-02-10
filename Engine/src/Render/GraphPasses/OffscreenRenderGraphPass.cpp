#include "Engine/Render/GraphPasses/OffscreenRenderGraphPass.hpp"

#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Shaders/ShaderDataExtractor.hpp"
#include "Core/Cache/GraphicsPipelineCache.hpp"

#include "Engine/Primitives.hpp"

#include "Core/Shader.hpp"
#include "Engine/Vertex.hpp"
#include "Core/VulkanHelpers.hpp"

#include "Engine/Builders/GraphicsPipelineBuilder.hpp"
#include "Engine/Builders/RenderPassBuilder.hpp"
#include "Engine/Render/RenderGraph/RenderGraph.hpp"

#include <iostream>

struct VertexStruct
{
    glm::vec3 position{1.0f};

    VertexStruct(const glm::vec3 &pos) : position(pos) {}
};

struct CameraUBO
{
    glm::mat4 view;
    glm::mat4 projection;
};

struct LightSpaceMatrixUBO
{
    glm::mat4 lightSpaceMatrix;
};

struct ModelOnly
{
    glm::mat4 model{1.0f};
};

struct ModelPushConstant
{
    glm::mat4 model{1.0f};
    uint32_t objectId{0};
    uint32_t padding[3];
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

OffscreenRenderGraphPass::OffscreenRenderGraphPass(VkDescriptorPool descriptorPool, RGPResourceHandler &shadowTextureHandler)
    : m_swapChain(core::VulkanContext::getContext()->getSwapchain()), m_descriptorPool(descriptorPool),
      m_shadowTextureHandler(shadowTextureHandler)
{
    m_device = core::VulkanContext::getContext()->getDevice();
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    m_clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    m_clearValues[2].depthStencil = {1.0f, 0};

    this->setDebugName("Offscreen render graph pass");

    m_perObjectDescriptorSets.resize(RenderGraph::MAX_FRAMES_IN_FLIGHT);

    m_bonesSSBOs.reserve(RenderGraph::MAX_FRAMES_IN_FLIGHT);

    static constexpr VkDeviceSize bonesStructSize = sizeof(glm::mat4);
    static constexpr uint8_t INIT_BONES_COUNT = 100;
    static constexpr VkDeviceSize INITIAL_SIZE = bonesStructSize * (INIT_BONES_COUNT * bonesStructSize);

    for (size_t i = 0; i < RenderGraph::MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto ssboBuffer = m_bonesSSBOs.emplace_back(core::Buffer::createShared(INITIAL_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                               core::memory::MemoryUsage::CPU_TO_GPU));

        m_perObjectDescriptorSets[i] = DescriptorSetBuilder::begin()
                                           .addBuffer(ssboBuffer, VK_WHOLE_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                           .build(m_device, m_descriptorPool, EngineShaderFamilies::objectDescriptorSetLayout->vk());
    }
}

void OffscreenRenderGraphPass::update(const RenderGraphPassContext &renderData)
{
    m_currentFrame = renderData.currentFrame;
    m_imageIndex = renderData.currentImageIndex;
}

void OffscreenRenderGraphPass::setViewport(VkViewport viewport)
{
    m_viewport = viewport;
}

void OffscreenRenderGraphPass::setScissor(VkRect2D scissor)
{
    m_scissor = scissor;
}

void OffscreenRenderGraphPass::getRenderPassBeginInfo(VkRenderPassBeginInfo &renderPassBeginInfo) const
{
    renderPassBeginInfo = VkRenderPassBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassBeginInfo.renderPass = m_renderPass->vk();
    renderPassBeginInfo.framebuffer = m_framebuffers[m_imageIndex]->vk();
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = m_swapChain.lock()->getExtent();
    renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(m_clearValues.size());
    renderPassBeginInfo.pClearValues = m_clearValues.data();
}

void OffscreenRenderGraphPass::onSwapChainResized(renderGraph::RGPResourcesStorage &storage)
{
    auto depthTexture = storage.getTexture(m_depthTextureHandler);
    auto objectIdTexture = storage.getTexture(m_objectIdTextureHandler);

    depthTexture->getImage()->transitionImageLayout(core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice()), VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        auto frameBuffer = m_framebuffers[imageIndex];

        auto colorTexture = storage.getTexture(m_colorTextureHandler[imageIndex]);
        m_colorImages[imageIndex] = colorTexture;

        std::vector<VkImageView> attachments{colorTexture->vkImageView(), objectIdTexture->vkImageView(), depthTexture->vkImageView()};

        frameBuffer->resize(core::VulkanContext::getContext()->getSwapchain()->getExtent(), attachments);
    }
}

void OffscreenRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    auto depthTexture = storage.getTexture(m_depthTextureHandler);
    auto objectIdTexture = storage.getTexture(m_objectIdTextureHandler);

    depthTexture->getImage()->transitionImageLayout(core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice()), VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        auto colorTexture = storage.getTexture(m_colorTextureHandler[imageIndex]);
        m_colorImages.push_back(colorTexture);
        std::vector<VkImageView> attachments{colorTexture->vkImageView(), objectIdTexture->vkImageView(), depthTexture->vkImageView()};

        auto framebuffer = core::Framebuffer::createShared(core::VulkanContext::getContext()->getDevice(), attachments,
                                                           m_renderPass, core::VulkanContext::getContext()->getSwapchain()->getExtent());

        m_framebuffers.push_back(framebuffer);
    }

    // std::array<std::string, 6> cubemaps{
    //     "./resources/textures/right.jpg",
    //     "./resources/textures/left.jpg",
    //     "./resources/textures/top.jpg",
    //     "./resources/textures/bottom.jpg",
    //     "./resources/textures/front.jpg",
    //     "./resources/textures/back.jpg",
    // };

    // m_skybox = std::make_unique<Skybox>(m_device, core::VulkanContext::getContext()->getPhysicalDevice(), core::VulkanContext::getContext()->getTransferCommandPool(), m_renderPass,
    //                                     cubemaps, m_descriptorPool);

    m_skybox = std::make_unique<Skybox>(m_renderPass, "./resources/textures/default_sky.hdr", m_descriptorPool);
}

void OffscreenRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    // auto renderPassBuilder = builders::RenderPassBuilder::begin();
    auto physicalDevice = core::VulkanContext::getContext()->getPhysicalDevice();

    // auto &colorAttachment = renderPassBuilder.addColorAttachment(m_swapChain.lock()->getImageFormat());
    // auto &objectIdAttachment = renderPassBuilder.addColorAttachment(VK_FORMAT_R32_UINT);
    // auto &depthStencilAttachment = renderPassBuilder.addDepthAttachment(core::helpers::findDepthFormat(physicalDevice));

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapChain.lock()->getImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription objectIdAttachment{};
    objectIdAttachment.format = VK_FORMAT_R32_UINT;
    objectIdAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    objectIdAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    objectIdAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    objectIdAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    objectIdAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    objectIdAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    objectIdAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = core::helpers::findDepthFormat(physicalDevice);
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::vector<VkAttachmentReference> attachmentReferences(2);
    attachmentReferences[0].attachment = 0;
    attachmentReferences[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachmentReferences[1].attachment = 1;
    attachmentReferences[1].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentReference{};
    depthAttachmentReference.attachment = 2;
    depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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
    des.pDepthStencilAttachment = &depthAttachmentReference;
    des.flags = 0;

    m_renderPass = core::RenderPass::createShared(
        std::vector<VkAttachmentDescription>{colorAttachment, objectIdAttachment, depthAttachment},
        std::vector<VkSubpassDescription>{des},
        std::vector<VkSubpassDependency>{dependency});

    renderGraph::RGPTextureDescription colorTextureDescription{};
    renderGraph::RGPTextureDescription depthTextureDescription{};
    renderGraph::RGPTextureDescription objectIdTextureDescription{};

    colorTextureDescription.setDebugName("__ELIX_COLOR_OFFSCREEN__TEXTURE__");
    colorTextureDescription.setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    colorTextureDescription.setFormat(core::VulkanContext::getContext()->getSwapchain()->getImageFormat());
    colorTextureDescription.setUsage(renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
    colorTextureDescription.setIsSwapChainTarget(false);

    objectIdTextureDescription.setDebugName("__ELIX_OBJECT_ID__TEXTURE__");
    objectIdTextureDescription.setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    objectIdTextureDescription.setFormat(VK_FORMAT_R32_UINT);
    objectIdTextureDescription.setUsage(renderGraph::RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC);
    objectIdTextureDescription.setIsSwapChainTarget(false);

    depthTextureDescription.setDebugName("__ELIX_DEPTH_OFFSCREEN__TEXTURE__");
    depthTextureDescription.setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    depthTextureDescription.setFormat(core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice()));
    depthTextureDescription.setUsage(renderGraph::RGPTextureUsage::DEPTH_STENCIL);
    depthTextureDescription.setIsSwapChainTarget(false);

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        colorTextureDescription.setDebugName("__ELIX_COLOR_OFFSCREEN__TEXTURE_" + std::to_string(imageIndex) + "__");
        auto colorTexture = builder.createTexture(colorTextureDescription);
        m_colorTextureHandler.push_back(colorTexture);
        builder.write(colorTexture, renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
    }

    m_objectIdTextureHandler = builder.createTexture(objectIdTextureDescription);

    m_depthTextureHandler = builder.createTexture(depthTextureDescription);
    builder.write(m_depthTextureHandler, renderGraph::RGPTextureUsage::DEPTH_STENCIL);

    builder.read(m_shadowTextureHandler, RGPTextureUsage::SAMPLED);

    auto shader = std::make_shared<core::Shader>("./resources/shaders/static_mesh.vert.spv", "./resources/shaders/static_mesh.frag.spv");
    auto skeletonShader = std::make_shared<core::Shader>("./resources/shaders/skeleton_mesh.vert.spv", "./resources/shaders/static_mesh.frag.spv");
    auto wireframeShader = std::make_shared<core::Shader>("./resources/shaders/wireframe_mesh.vert.spv", "./resources/shaders/debug_red.frag.spv");
    auto stencilShader = std::make_shared<core::Shader>("./resources/shaders/wireframe_mesh.vert.spv", "./resources/shaders/debug_yellow.frag.spv");
    // ShaderDataExtractor::parse(shader->getVertexHandler());

    //? what is this parameter
    const uint32_t subpass = 0;
    const VkViewport viewport = core::VulkanContext::getContext()->getSwapchain()->getViewport();
    const VkRect2D scissor = core::VulkanContext::getContext()->getSwapchain()->getScissor();
    std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::Vertex3D))};
    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions = {vertex::Vertex3D::getAttributeDescriptions()};

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
        builders::GraphicsPipelineBuilder::colorBlendAttachmentCI(false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO),
        builders::GraphicsPipelineBuilder::colorBlendAttachmentCI(false, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO)};

    colorBlendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;

    auto colorBlending = builders::GraphicsPipelineBuilder::colorBlending(colorBlendAttachments);

    m_graphicsPipeline = core::GraphicsPipeline::createShared(device, m_renderPass->vk(), shader->getShaderStages(), EngineShaderFamilies::staticMeshShaderFamily.pipelineLayout->vk(),
                                                              dynamicState, colorBlending, multisampling, rasterizer, viewportState, inputAssembly, vertexInputState,
                                                              subpass, depthStencil, cache);

    {
        vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::VertexSkinned))};
        vertexAttributeDescriptions = {vertex::VertexSkinned::getAttributeDescriptions()};

        vertexInputState = builders::GraphicsPipelineBuilder::vertexInputCI(vertexBindingDescriptions, vertexAttributeDescriptions);

        m_skeletalGraphicsPipeline = core::GraphicsPipeline::createShared(device, m_renderPass->vk(), skeletonShader->getShaderStages(), EngineShaderFamilies::skeletonMeshShaderFamily.pipelineLayout->vk(),
                                                                          dynamicState, colorBlending, multisampling, rasterizer, viewportState, inputAssembly, vertexInputState,
                                                                          subpass, depthStencil, cache);
    }

    // {
    //     std::vector<VkVertexInputAttributeDescription> attributes(1);

    //     attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexStruct, position)};

    //     vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(VertexStruct))};
    //     vertexAttributeDescriptions = attributes;

    //     vertexInputState = builders::GraphicsPipelineBuilder::vertexInputCI(vertexBindingDescriptions, vertexAttributeDescriptions);

    //     auto beforeRasterizer = rasterizer;
    //     rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    //     rasterizer.lineWidth = 1.0f;
    //     rasterizer.cullMode = VK_CULL_MODE_NONE;
    //     rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    //     rasterizer.depthBiasEnable = VK_FALSE;

    //     auto assemblyBefore = inputAssembly;
    //     inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    //     m_wireframeGraphicsPipeline = core::GraphicsPipeline::createShared(device, m_renderPass->vk(), wireframeShader->getShaderStages(), EngineShaderFamilies::wireframeMeshShaderFamily.pipelineLayout->vk(),
    //                                                                            dynamicState, colorBlending, multisampling, rasterizer, viewportState, inputAssembly, vertexInputState,
    //                                                                            subpass, depthStencil, cache);

    //     rasterizer = beforeRasterizer;
    //     inputAssembly = assemblyBefore;
    // }

    {
        std::vector<VkVertexInputAttributeDescription> attributes(1);

        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexStruct, position)};

        vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(VertexStruct))};
        vertexAttributeDescriptions = attributes;

        vertexInputState = builders::GraphicsPipelineBuilder::vertexInputCI(vertexBindingDescriptions, vertexAttributeDescriptions);

        rasterizer = builders::GraphicsPipelineBuilder::rasterizationCI(VK_POLYGON_MODE_FILL);
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        inputAssembly = builders::GraphicsPipelineBuilder::inputAssemblyCI(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

        depthStencil = builders::GraphicsPipelineBuilder::depthStencilCI(false, false, VK_COMPARE_OP_LESS_OR_EQUAL);

        m_stencilGraphicsPipeline = core::GraphicsPipeline::createShared(device, m_renderPass->vk(), stencilShader->getShaderStages(), EngineShaderFamilies::wireframeMeshShaderFamily.pipelineLayout->vk(),
                                                                         dynamicState, colorBlending, multisampling, rasterizer, viewportState, inputAssembly, vertexInputState,
                                                                         subpass, depthStencil, cache);
    }
}

void OffscreenRenderGraphPass::cleanup()
{
}

void OffscreenRenderGraphPass::execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data)
{
    //----Later
    // vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    // vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);
    //----

    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &data.swapChainViewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &data.swapChainScissor);

    for (const auto &[entity, gpuEntity] : data.meshes)
    {
        uint64_t entityId = entity->getId();

        for (const auto &mesh : gpuEntity.meshes)
        {
            core::GraphicsPipeline::SharedPtr graphicsPipeline = m_graphicsPipeline;
            core::PipelineLayout::SharedPtr pipelineLayout = EngineShaderFamilies::staticMeshShaderFamily.pipelineLayout;

            graphicsPipeline = gpuEntity.finalBones.empty() ? m_graphicsPipeline : m_skeletalGraphicsPipeline;
            pipelineLayout = gpuEntity.finalBones.empty() ? EngineShaderFamilies::staticMeshShaderFamily.pipelineLayout : EngineShaderFamilies::skeletonMeshShaderFamily.pipelineLayout;

            vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->vk());

            VkBuffer vertexBuffers[] = {mesh->vertexBuffer->vk()};
            VkDeviceSize offset[] = {0};

            vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);
            vkCmdBindIndexBuffer(commandBuffer->vk(), mesh->indexBuffer->vk(), 0, mesh->indexType);

            ModelPushConstant modelPushConstant{
                .model = gpuEntity.transform, .objectId = entityId};

            // vkCmdPushConstants(commandBuffer->vk(), pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ModelPushConstant), &modelPushConstant);
            vkCmdPushConstants(commandBuffer->vk(), pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ModelPushConstant), &modelPushConstant);
            std::vector<VkDescriptorSet> descriptorSets;

            if (gpuEntity.finalBones.empty())
            {
                descriptorSets =
                    {
                        data.cameraDescriptorSet,                         // set 0: camera & light
                        mesh->material->getDescriptorSet(m_currentFrame), // set 1: material
                    };
            }
            else
            {
                descriptorSets =
                    {
                        data.cameraDescriptorSet,                         // set 0: camera & light
                        mesh->material->getDescriptorSet(m_currentFrame), // set 1: material
                        m_perObjectDescriptorSets[m_currentFrame]};

                glm::mat4 *mapped;
                m_bonesSSBOs[m_currentFrame]->map(reinterpret_cast<void *&>(mapped));
                for (int i = 0; i < gpuEntity.finalBones.size(); ++i)
                    mapped[i] = gpuEntity.finalBones.at(i);

                // void *mapped;
                // m_bonesSSBOs[m_currentFrame]->map(mapped);
                // BonesSSBO *ssboData = static_cast<BonesSSBO *>(mapped);

                // ssboData->bonesCount = static_cast<int>(gpuEntity.finalBones.size());

                // for (int i = 0; i < gpuEntity.finalBones.size(); ++i)
                // {
                //     ssboData->boneMatrices[i] = gpuEntity.finalBones.at(i);
                // }

                m_bonesSSBOs[m_currentFrame]->unmap();
            }

            vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout->vk(), 0, static_cast<uint32_t>(descriptorSets.size()),
                                    descriptorSets.data(), 0, nullptr);

            vkCmdDrawIndexed(commandBuffer->vk(), mesh->indicesCount, 1, 0, 0, 0);
        }
    }

    m_skybox->render(commandBuffer, data.view, data.projection);

    for (auto &addData : data.additionalData)
    {
        for (const auto &wireframeEntity : addData.stencilMeshes)
        {
            for (const auto &m : wireframeEntity.meshes)
            {
                core::PipelineLayout::SharedPtr pipelineLayout = EngineShaderFamilies::wireframeMeshShaderFamily.pipelineLayout;

                vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_stencilGraphicsPipeline->vk());

                VkBuffer vertexBuffers[] = {m->vertexBuffer->vk()};
                VkDeviceSize offset[] = {0};

                vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);
                vkCmdBindIndexBuffer(commandBuffer->vk(), m->indexBuffer->vk(), 0, m->indexType);

                ModelOnly modelPushConstant{
                    .model = wireframeEntity.transform};

                vkCmdPushConstants(commandBuffer->vk(), pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ModelOnly), &modelPushConstant);

                std::vector<VkDescriptorSet> descriptorSets;

                descriptorSets =
                    {
                        data.cameraDescriptorSet // set 0: camera
                    };
                vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout->vk(), 0, static_cast<uint32_t>(descriptorSets.size()),
                                        descriptorSets.data(), 0, nullptr);

                vkCmdDrawIndexed(commandBuffer->vk(), m->indicesCount, 1, 0, 0, 0);
            }
        }
    }
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END