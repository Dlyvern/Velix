#include "Engine/Render/GraphPasses/SceneRenderGraphPass.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include "Core/Shader.hpp"
#include "Core/VulkanHelpers.hpp"
#include <iostream>

// struct ModelPushConstant
// {
//     glm::mat4 model{1.0f};
//     uint32_t objectId{0};
//     uint32_t padding[3];
// };

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

SceneRenderGraphPass::SceneRenderGraphPass(renderGraph::RGPResourceHandler &shadowHandler) : m_shadowHandler(shadowHandler)
{
    // m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    // m_clearValues[1].color = {0.0f, 0.0f, 0.0f, 0.0f};
    // m_clearValues[2].depthStencil = {1.0f, 0};
    // this->setDebugName("Screne render graph pass");
}

void SceneRenderGraphPass::setup(RGPResourcesBuilder &builder)
{
    // VkAttachmentDescription colorAttachment{};
    // colorAttachment.format = core::VulkanContext::getContext()->getSwapchain()->getImageFormat();
    // colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    // colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    // colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // VkAttachmentDescription objectIdAttachment{};
    // objectIdAttachment.format = VK_FORMAT_R32_UINT;
    // objectIdAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    // objectIdAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // objectIdAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // objectIdAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    // objectIdAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // objectIdAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // objectIdAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // VkAttachmentDescription depthAttachment{};
    // depthAttachment.format = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());
    // depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    // depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // VkAttachmentReference colorAttachmentReferences[2];
    // colorAttachmentReferences[0].attachment = 0;
    // colorAttachmentReferences[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // colorAttachmentReferences[1].attachment = 1;
    // colorAttachmentReferences[1].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // VkAttachmentReference depthAttachmentReference{};
    // depthAttachmentReference.attachment = 2;
    // depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // VkSubpassDependency dependency{};
    // dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    // dependency.dstSubpass = 0;
    // dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    // dependency.srcAccessMask = 0;
    // dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    // dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // VkSubpassDescription des{};
    // des.colorAttachmentCount = 2;
    // des.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    // des.pColorAttachments = colorAttachmentReferences;
    // des.pDepthStencilAttachment = &depthAttachmentReference;
    // des.flags = 0;

    // m_renderPass = core::RenderPass::createShared(
    //     std::vector<VkAttachmentDescription>{colorAttachment, objectIdAttachment, depthAttachment},
    //     std::vector<VkSubpassDescription>{des},
    //     std::vector<VkSubpassDependency>{dependency});

    // RGPTextureDescription colorTextureDescription{};
    // RGPTextureDescription depthTextureDescription{};
    // RGPTextureDescription objectIdTextureDescription{};

    // colorTextureDescription.setDebugName("__ELIX_COLOR_TEXTURE__");
    // colorTextureDescription.setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    // colorTextureDescription.setFormat(core::VulkanContext::getContext()->getSwapchain()->getImageFormat());
    // colorTextureDescription.setUsage(RGPTextureUsage::COLOR_ATTACHMENT);
    // colorTextureDescription.setIsSwapChainTarget(true);

    // objectIdTextureDescription.setDebugName("__ELIX_OBJECT_ID_TEXTURE__");
    // objectIdTextureDescription.setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    // objectIdTextureDescription.setFormat(VK_FORMAT_R32_UINT);
    // objectIdTextureDescription.setUsage(RGPTextureUsage::COLOR_ATTACHMENT_TRANSFER_SRC);

    // depthTextureDescription.setDebugName("__ELIX_DEPTH_TEXTURE__");
    // depthTextureDescription.setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    // depthTextureDescription.setFormat(core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice()));
    // depthTextureDescription.setUsage(RGPTextureUsage::DEPTH_STENCIL);

    // m_colorTextureHandler = builder.createTexture(colorTextureDescription);
    // m_depthTextureHandler = builder.createTexture(depthTextureDescription);
    // m_objectIdTextureHandler = builder.createTexture(objectIdTextureDescription);

    // builder.write(m_colorTextureHandler, RGPTextureUsage::COLOR_ATTACHMENT);
    // builder.write(m_depthTextureHandler, RGPTextureUsage::DEPTH_STENCIL);
    // builder.write(m_objectIdTextureHandler, RGPTextureUsage::COLOR_ATTACHMENT);

    // builder.read(m_shadowHandler, RGPTextureUsage::SAMPLED);
}

void SceneRenderGraphPass::compile(RGPResourcesStorage &storage)
{
    // auto depthTexture = storage.getTexture(m_depthTextureHandler);
    // auto objectIdTexture = storage.getTexture(m_objectIdTextureHandler);

    // depthTexture->getImage()->transitionImageLayout(core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice()), VK_IMAGE_LAYOUT_UNDEFINED,
    //                                                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    // for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    // {
    //     auto colorTexture = storage.getSwapChainTexture(m_colorTextureHandler, imageIndex);
    //     std::vector<VkImageView> attachments{colorTexture->vkImageView(), objectIdTexture->vkImageView(), depthTexture->vkImageView()};

    //     auto framebuffer = core::Framebuffer::createShared(core::VulkanContext::getContext()->getDevice(), attachments,
    //                                                        m_renderPass, core::VulkanContext::getContext()->getSwapchain()->getExtent());

    //     m_framebuffers.push_back(framebuffer);
    // }

    // VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    // VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    // VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    // VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    // VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    // VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    // VkPipelineDynamicStateCreateInfo dynamicState{};
    // std::vector<VkDynamicState> dynamicStates;

    // std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions;
    // std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;

    // VkPipelineLayout layout = VK_NULL_HANDLE;
    // uint32_t subpass = 0;

    // std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    // inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    // inputAssembly.primitiveRestartEnable = VK_FALSE;

    // rasterizer.depthClampEnable = VK_FALSE;
    // rasterizer.rasterizerDiscardEnable = VK_FALSE;
    // rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    // rasterizer.lineWidth = 1.0f;
    // rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    // rasterizer.depthBiasEnable = VK_FALSE;
    // rasterizer.depthBiasConstantFactor = 0.0f;
    // rasterizer.depthBiasSlopeFactor = 0.0f;
    // rasterizer.depthBiasClamp = 0.0f;
    // rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;

    // multisampling.sampleShadingEnable = VK_FALSE;
    // multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    // multisampling.minSampleShading = 1.0f;
    // multisampling.pSampleMask = nullptr;
    // multisampling.alphaToCoverageEnable = VK_FALSE;
    // multisampling.alphaToOneEnable = VK_FALSE;

    // depthStencil.depthTestEnable = VK_TRUE;
    // depthStencil.depthWriteEnable = VK_TRUE;
    // depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    // depthStencil.depthBoundsTestEnable = VK_FALSE;
    // depthStencil.minDepthBounds = 0.0f;
    // depthStencil.maxDepthBounds = 1.0f;
    // depthStencil.stencilTestEnable = VK_FALSE;
    // depthStencil.front = {};
    // depthStencil.back = {};

    // // VkPipelineColorBlendAttachmentState colorBlendAttachment{
    // //     .blendEnable = VK_FALSE,
    // //     .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
    // //     .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
    // //     .colorBlendOp = VK_BLEND_OP_ADD,
    // //     .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    // //     .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    // //     .alphaBlendOp = VK_BLEND_OP_ADD,
    // //     .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};

    // VkPipelineColorBlendAttachmentState colorBlendAttachments[2]{};

    // for (int i = 0; i < 2; i++)
    // {
    //     colorBlendAttachments[i].blendEnable = VK_FALSE;
    //     colorBlendAttachments[i].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    //     colorBlendAttachments[i].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    //     colorBlendAttachments[i].colorBlendOp = VK_BLEND_OP_ADD;
    //     colorBlendAttachments[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    //     colorBlendAttachments[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    //     colorBlendAttachments[i].alphaBlendOp = VK_BLEND_OP_ADD;
    // }

    // colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
    //                                           VK_COLOR_COMPONENT_G_BIT |
    //                                           VK_COLOR_COMPONENT_B_BIT |
    //                                           VK_COLOR_COMPONENT_A_BIT;

    // colorBlendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;

    // colorBlending.blendConstants[0] = 0.0f;
    // colorBlending.blendConstants[1] = 0.0f;
    // colorBlending.blendConstants[2] = 0.0f;
    // colorBlending.blendConstants[3] = 0.0f;

    // colorBlending.logicOpEnable = VK_FALSE;
    // colorBlending.attachmentCount = 2;
    // colorBlending.pAttachments = colorBlendAttachments;

    // VkViewport viewport = core::VulkanContext::getContext()->getSwapchain()->getViewport();
    // VkRect2D scissor = core::VulkanContext::getContext()->getSwapchain()->getScissor();
    // viewportState.viewportCount = 1;
    // viewportState.scissorCount = 1;
    // viewportState.pViewports = &viewport;
    // viewportState.pScissors = &scissor;

    // dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    // dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    // dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
    // dynamicState.pDynamicStates = dynamicStates.data();

    // auto shader = std::make_shared<core::Shader>("./resources/shaders/static_mesh.vert.spv", "./resources/shaders/static_mesh.frag.spv");

    // shaderStages = shader->getShaderStages();

    // vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::Vertex3D))};
    // vertexAttributeDescriptions = {vertex::Vertex3D::getAttributeDescriptions()};

    // VkPipelineVertexInputStateCreateInfo vertexInputStateCI{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    // vertexInputStateCI.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindingDescriptions.size());
    // vertexInputStateCI.pVertexBindingDescriptions = vertexBindingDescriptions.data();
    // vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescriptions.size());
    // vertexInputStateCI.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

    // m_graphicsPipeline = core::GraphicsPipeline::createShared(core::VulkanContext::getContext()->getDevice(), m_renderPass->vk(), shaderStages, EngineShaderFamilies::meshShaderFamily.pipelineLayout->vk(),
    //                                                           dynamicState, colorBlending, multisampling, rasterizer, viewportState, inputAssembly, vertexInputStateCI,
    //                                                           subpass, depthStencil);
}

void SceneRenderGraphPass::onSwapChainResized(renderGraph::RGPResourcesStorage &storage)
{
    // auto depthTexture = storage.getTexture(m_depthTextureHandler);
    // auto objectIdTexture = storage.getTexture(m_objectIdTextureHandler);

    // depthTexture->getImage()->transitionImageLayout(core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice()), VK_IMAGE_LAYOUT_UNDEFINED,
    //                                                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    // for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    // {
    //     auto frameBuffer = m_framebuffers[imageIndex];

    //     auto colorTexture = storage.getSwapChainTexture(m_colorTextureHandler, imageIndex);
    //     std::vector<VkImageView> attachments{colorTexture->vkImageView(), objectIdTexture->vkImageView(), depthTexture->vkImageView()};

    //     frameBuffer->resize(core::VulkanContext::getContext()->getSwapchain()->getExtent(), attachments);
    // }
}

std::vector<IRenderGraphPass::RenderPassExecution> SceneRenderGraphPass::getRenderPassExecutions(const RenderGraphPassContext &renderContext) const
{
    // IRenderGraphPass::RenderPassExecution renderPassExecution;
    // renderPassExecution.clearValues = {m_clearValues[0], m_clearValues[1], m_clearValues[2]};
    // renderPassExecution.framebuffer = m_framebuffers[renderContext.currentImageIndex];
    // renderPassExecution.renderArea.offset = {0, 0};
    // renderPassExecution.renderArea.extent = core::VulkanContext::getContext()->getSwapchain()->getExtent();
    // renderPassExecution.renderPass = m_renderPass;
    // return {renderPassExecution};
}

void SceneRenderGraphPass::record(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data,
                                  const RenderGraphPassContext &renderContext)
{
    // vkCmdSetViewport(commandBuffer->vk(), 0, 1, &data.swapChainViewport);
    // vkCmdSetScissor(commandBuffer->vk(), 0, 1, &data.swapChainScissor);

    // vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline->vk());

    // auto &pipelineLayout = EngineShaderFamilies::meshShaderFamily.pipelineLayout;

    // for (const auto &[entity, gpuEntity] : data.drawItems)
    // {
    //     uint64_t entityId = entity->getId();

    //     for (const auto &mesh : gpuEntity.meshes)
    //     {
    //         VkBuffer vertexBuffers[] = {mesh->vertexBuffer->vk()};
    //         VkDeviceSize offset[] = {0};

    //         vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);
    //         vkCmdBindIndexBuffer(commandBuffer->vk(), mesh->indexBuffer->vk(), 0, mesh->indexType);

    //         ModelPushConstant modelPushConstant{
    //             .model = gpuEntity.transform, .objectId = entityId};

    //         vkCmdPushConstants(commandBuffer->vk(), pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
    //                            sizeof(ModelPushConstant), &modelPushConstant);

    //         const std::array<VkDescriptorSet, 3> descriptorSets =
    //             {
    //                 data.cameraDescriptorSet,                                     // set 0: camera & light
    //                 mesh->material->getDescriptorSet(renderContext.currentFrame), // set 1: material
    //             };

    //         vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout->vk(), 0, static_cast<uint32_t>(descriptorSets.size()),
    //                                 descriptorSets.data(), 0, nullptr);

    //         vkCmdDrawIndexed(commandBuffer->vk(), mesh->indicesCount, 1, 0, 0, 0);
    //     }
    // }

    // m_skybox->render(commandBuffer, data.view, data.projection);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END