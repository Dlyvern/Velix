#include "Engine/Render/GraphPasses/OffscreenRenderGraphPass.hpp"

#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Shaders/ShaderDataExtractor.hpp"

#include "Engine/Primitives.hpp"

#include "Core/Shader.hpp"
#include "Engine/Vertex.hpp"
#include "Core/VulkanHelpers.hpp"

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

struct ModelPushConstant
{
    glm::mat4 model{1.0f};
};

struct BonesSSBO
{
    int bonesCount;
    glm::vec3 allign;
    glm::mat4 boneMatrices[];
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

OffscreenRenderGraphPass::OffscreenRenderGraphPass(VkDescriptorPool descriptorPool) : m_swapChain(core::VulkanContext::getContext()->getSwapchain()), m_descriptorPool(descriptorPool)
{
    m_device = core::VulkanContext::getContext()->getDevice();
    m_clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    m_clearValues[1].depthStencil = {1.0f, 0};
}

void OffscreenRenderGraphPass::createSkeleton(VkSampler sampler, VkImageView imageView, int maxFramesInFlight)
{
    m_cameraDescriptorSets.resize(maxFramesInFlight);
    m_cameraWireframeDescriptorSets.resize(maxFramesInFlight);

    m_cameraMapped.resize(maxFramesInFlight);
    m_lightMapped.resize(maxFramesInFlight);

    m_lightSpaceMatrixUniformObjects.reserve(maxFramesInFlight);
    m_cameraUniformObjects.reserve(maxFramesInFlight);

    m_bonesSSBOs.reserve(maxFramesInFlight);

    m_cameraWireframeMapped.resize(maxFramesInFlight);
    m_cameraWireframeUniformObjects.reserve(maxFramesInFlight);

    static constexpr uint8_t INIT_BONES_COUNT = 100;
    static constexpr VkDeviceSize INITIAL_SIZE = sizeof(BonesSSBO) * (INIT_BONES_COUNT * sizeof(BonesSSBO));

    for (size_t i = 0; i < maxFramesInFlight; ++i)
    {
        auto &cameraBuffer = m_cameraUniformObjects.emplace_back(core::Buffer::createShared(sizeof(CameraUBO),
                                                                                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        auto &lightBuffer = m_lightSpaceMatrixUniformObjects.emplace_back(core::Buffer::createShared(sizeof(LightSpaceMatrixUBO),
                                                                                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        auto ssboBuffer = m_bonesSSBOs.emplace_back(core::Buffer::createShared(INITIAL_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                               core::memory::MemoryUsage::CPU_TO_GPU));

        cameraBuffer->map(m_cameraMapped[i]);
        lightBuffer->map(m_lightMapped[i]);

        m_cameraDescriptorSets[i] = DescriptorSetBuilder::begin()
                                        .addBuffer(cameraBuffer, sizeof(CameraUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                        .addBuffer(lightBuffer, sizeof(LightSpaceMatrixUBO), 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                        .addImage(imageView, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2)
                                        .addBuffer(ssboBuffer, VK_WHOLE_SIZE, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                        .build(m_device, m_descriptorPool, engineShaderFamilies::skeletonMeshCameraLayout->vk());

        auto &wireframeCameraBuffer = m_cameraWireframeUniformObjects.emplace_back(core::Buffer::createShared(sizeof(CameraUBO),
                                                                                                              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        wireframeCameraBuffer->map(m_cameraWireframeMapped[i]);
        m_cameraWireframeDescriptorSets[i] = DescriptorSetBuilder::begin()
                                                 .addBuffer(wireframeCameraBuffer, sizeof(CameraUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                                 .build(m_device, m_descriptorPool, engineShaderFamilies::wireframeMeshCameraLayout->vk());
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

    depthTexture->getImage()->transitionImageLayout(core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice()), VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        auto frameBuffer = m_framebuffers[imageIndex];

        auto colorTexture = storage.getTexture(m_colorTextureHandler[imageIndex]);
        m_colorImages[imageIndex] = colorTexture;

        std::vector<VkImageView> attachments{colorTexture->vkImageView(), depthTexture->vkImageView()};

        frameBuffer->resize(core::VulkanContext::getContext()->getSwapchain()->getExtent(), attachments);
    }
}

void OffscreenRenderGraphPass::compile(renderGraph::RGPResourcesStorage &storage)
{
    auto depthTexture = storage.getTexture(m_depthTextureHandler);
    depthTexture->getImage()->transitionImageLayout(core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice()), VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    for (int imageIndex = 0; imageIndex < core::VulkanContext::getContext()->getSwapchain()->getImages().size(); ++imageIndex)
    {
        auto colorTexture = storage.getTexture(m_colorTextureHandler[imageIndex]);
        m_colorImages.push_back(colorTexture);
        std::vector<VkImageView> attachments{colorTexture->vkImageView(), depthTexture->vkImageView()};

        auto framebuffer = std::make_shared<core::Framebuffer>(core::VulkanContext::getContext()->getDevice(), attachments,
                                                               m_renderPass, core::VulkanContext::getContext()->getSwapchain()->getExtent());

        m_framebuffers.push_back(framebuffer);
    }

    std::array<std::string, 6> cubemaps{
        "./resources/textures/right.jpg",
        "./resources/textures/left.jpg",
        "./resources/textures/top.jpg",
        "./resources/textures/bottom.jpg",
        "./resources/textures/front.jpg",
        "./resources/textures/back.jpg",
    };

    m_skybox = std::make_unique<Skybox>(m_device, core::VulkanContext::getContext()->getPhysicalDevice(), core::VulkanContext::getContext()->getTransferCommandPool(), m_renderPass,
                                        cubemaps, m_descriptorPool);
}

void OffscreenRenderGraphPass::setup(renderGraph::RGPResourcesBuilder &builder)
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapChain.lock()->getImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = core::helpers::findDepthFormat(core::VulkanContext::getContext()->getPhysicalDevice());
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentReference{};
    colorAttachmentReference.attachment = 0;
    colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentReference{};
    depthAttachmentReference.attachment = 1;
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
    des.colorAttachmentCount = 1;
    des.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    des.pColorAttachments = &colorAttachmentReference;
    des.pDepthStencilAttachment = &depthAttachmentReference;
    des.flags = 0;

    m_renderPass = core::RenderPass::create({colorAttachment, depthAttachment}, {des},
                                            {dependency});

    renderGraph::RGPTextureDescription colorTextureDescription{};
    renderGraph::RGPTextureDescription depthTextureDescription{};

    colorTextureDescription.setDebugName("__ELIX_COLOR_OFFSCREEN__TEXTURE__");
    colorTextureDescription.setExtent(core::VulkanContext::getContext()->getSwapchain()->getExtent());
    colorTextureDescription.setFormat(core::VulkanContext::getContext()->getSwapchain()->getImageFormat());
    colorTextureDescription.setUsage(renderGraph::RGPTextureUsage::COLOR_ATTACHMENT);
    colorTextureDescription.setIsSwapChainTarget(false);

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

    m_depthTextureHandler = builder.createTexture(depthTextureDescription);
    builder.write(m_depthTextureHandler, renderGraph::RGPTextureUsage::DEPTH_STENCIL);

    auto shader = std::make_shared<core::Shader>("./resources/shaders/static_mesh.vert.spv", "./resources/shaders/static_mesh.frag.spv");
    auto skeletonShader = std::make_shared<core::Shader>("./resources/shaders/skeleton_mesh.vert.spv", "./resources/shaders/static_mesh.frag.spv");
    auto wireframeShader = std::make_shared<core::Shader>("./resources/shaders/wireframe_mesh.vert.spv", "./resources/shaders/debug_red.frag.spv");
    auto stencilShader = std::make_shared<core::Shader>("./resources/shaders/wireframe_mesh.vert.spv", "./resources/shaders/debug_yellow.frag.spv");
    // ShaderDataExtractor::parse(shader->getVertexHandler());

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    std::vector<VkDynamicState> dynamicStates;

    std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    uint32_t subpass = 0;

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;

    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {};
    depthStencil.back = {};

    VkPipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};

    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkViewport viewport = core::VulkanContext::getContext()->getSwapchain()->getViewport();
    VkRect2D scissor = core::VulkanContext::getContext()->getSwapchain()->getScissor();
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.pScissors = &scissor;

    dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
    dynamicState.pDynamicStates = dynamicStates.data();

    shaderStages = shader->getShaderStages();

    vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::Vertex3D))};
    vertexAttributeDescriptions = {vertex::Vertex3D::getAttributeDescriptions()};

    VkPipelineVertexInputStateCreateInfo vertexInputStateCI{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputStateCI.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindingDescriptions.size());
    vertexInputStateCI.pVertexBindingDescriptions = vertexBindingDescriptions.data();
    vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescriptions.size());
    vertexInputStateCI.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

    m_graphicsPipeline = std::make_shared<core::GraphicsPipeline>(core::VulkanContext::getContext()->getDevice(), m_renderPass->vk(), shaderStages.data(),
                                                                  static_cast<uint32_t>(shaderStages.size()), engineShaderFamilies::staticMeshShaderFamily.pipelineLayout->vk(),
                                                                  dynamicState, colorBlending, multisampling, rasterizer, viewportState, inputAssembly, vertexInputStateCI,
                                                                  subpass, depthStencil);

    {
        vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(vertex::VertexSkinned))};
        vertexAttributeDescriptions = {vertex::VertexSkinned::getAttributeDescriptions()};

        vertexInputStateCI.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindingDescriptions.size());
        vertexInputStateCI.pVertexBindingDescriptions = vertexBindingDescriptions.data();
        vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescriptions.size());
        vertexInputStateCI.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

        shaderStages = skeletonShader->getShaderStages();

        m_skeletalGraphicsPipeline = std::make_shared<core::GraphicsPipeline>(core::VulkanContext::getContext()->getDevice(), m_renderPass->vk(), shaderStages.data(),
                                                                              static_cast<uint32_t>(shaderStages.size()), engineShaderFamilies::skeletonMeshShaderFamily.pipelineLayout->vk(),
                                                                              dynamicState, colorBlending, multisampling, rasterizer, viewportState, inputAssembly, vertexInputStateCI,
                                                                              subpass, depthStencil);
    }

    {
        std::vector<VkVertexInputAttributeDescription> attributes(1);

        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexStruct, position)};

        vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(VertexStruct))};
        vertexAttributeDescriptions = attributes;

        vertexInputStateCI.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindingDescriptions.size());
        vertexInputStateCI.pVertexBindingDescriptions = vertexBindingDescriptions.data();
        vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescriptions.size());
        vertexInputStateCI.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

        auto beforeRasterizer = rasterizer;
        rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        auto assemblyBefore = inputAssembly;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

        shaderStages = wireframeShader->getShaderStages();

        m_wireframeGraphicsPipeline = std::make_shared<core::GraphicsPipeline>(core::VulkanContext::getContext()->getDevice(), m_renderPass->vk(), shaderStages.data(),
                                                                               static_cast<uint32_t>(shaderStages.size()), engineShaderFamilies::wireframeMeshShaderFamily.pipelineLayout->vk(),
                                                                               dynamicState, colorBlending, multisampling, rasterizer, viewportState, inputAssembly, vertexInputStateCI,
                                                                               subpass, depthStencil);

        rasterizer = beforeRasterizer;
        inputAssembly = assemblyBefore;
    }

    {
        std::vector<VkVertexInputAttributeDescription> attributes(1);

        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexStruct, position)};

        vertexBindingDescriptions = {vertex::getBindingDescription(sizeof(VertexStruct))};
        vertexAttributeDescriptions = attributes;

        vertexInputStateCI.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindingDescriptions.size());
        vertexInputStateCI.pVertexBindingDescriptions = vertexBindingDescriptions.data();
        vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescriptions.size());
        vertexInputStateCI.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

        auto beforeRasterizer = rasterizer;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        auto assemblyBefore = inputAssembly;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        shaderStages = stencilShader->getShaderStages();

        m_stencilGraphicsPipeline = std::make_shared<core::GraphicsPipeline>(core::VulkanContext::getContext()->getDevice(), m_renderPass->vk(), shaderStages.data(),
                                                                             static_cast<uint32_t>(shaderStages.size()), engineShaderFamilies::wireframeMeshShaderFamily.pipelineLayout->vk(),
                                                                             dynamicState, colorBlending, multisampling, rasterizer, viewportState, inputAssembly, vertexInputStateCI,
                                                                             subpass, depthStencil);

        rasterizer = beforeRasterizer;
        inputAssembly = assemblyBefore;
    }
}

void OffscreenRenderGraphPass::execute(core::CommandBuffer::SharedPtr commandBuffer, const RenderGraphPassPerFrameData &data)
{
    std::memcpy(m_lightMapped[m_currentFrame], &data.lightSpaceMatrix, sizeof(LightSpaceMatrixUBO));

    CameraUBO cameraUBO{};
    cameraUBO.projection = data.projection;
    cameraUBO.view = data.view;

    std::memcpy(m_cameraMapped[m_currentFrame], &cameraUBO, sizeof(CameraUBO));

    std::memcpy(m_cameraWireframeMapped[m_currentFrame], &cameraUBO, sizeof(CameraUBO));

    //----Later
    // vkCmdSetViewport(commandBuffer->vk(), 0, 1, &m_viewport);
    // vkCmdSetScissor(commandBuffer->vk(), 0, 1, &m_scissor);
    //----

    vkCmdSetViewport(commandBuffer->vk(), 0, 1, &data.swapChainViewport);
    vkCmdSetScissor(commandBuffer->vk(), 0, 1, &data.swapChainScissor);

    for (const auto &[entity, gpuEntity] : data.meshes)
    {
        for (const auto &mesh : gpuEntity.meshes)
        {
            core::GraphicsPipeline::SharedPtr graphicsPipeline = m_graphicsPipeline;
            core::PipelineLayout::SharedPtr pipelineLayout = engineShaderFamilies::staticMeshShaderFamily.pipelineLayout;

            graphicsPipeline = gpuEntity.finalBones.empty() ? m_graphicsPipeline : m_skeletalGraphicsPipeline;
            pipelineLayout = gpuEntity.finalBones.empty() ? engineShaderFamilies::staticMeshShaderFamily.pipelineLayout : engineShaderFamilies::skeletonMeshShaderFamily.pipelineLayout;

            vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->vk());

            VkBuffer vertexBuffers[] = {mesh->vertexBuffer->vk()};
            VkDeviceSize offset[] = {0};

            vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);
            vkCmdBindIndexBuffer(commandBuffer->vk(), mesh->indexBuffer->vk(), 0, mesh->indexType);

            ModelPushConstant modelPushConstant{
                .model = gpuEntity.transform};

            vkCmdPushConstants(commandBuffer->vk(), pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ModelPushConstant), &modelPushConstant);

            std::vector<VkDescriptorSet> descriptorSets;

            if (gpuEntity.finalBones.empty())
            {
                descriptorSets =
                    {
                        data.cameraDescriptorSet,                         // set 0: camera
                        mesh->material->getDescriptorSet(m_currentFrame), // set 1: material
                        data.lightDescriptorSet                           // set 2: lighting
                    };
            }
            else
            {
                descriptorSets =
                    {
                        m_cameraDescriptorSets[m_currentFrame],           // set 0: camera
                        mesh->material->getDescriptorSet(m_currentFrame), // set 1: material
                        data.lightDescriptorSet                           // set 2: lighting
                    };

                void *mapped;
                m_bonesSSBOs[m_currentFrame]->map(mapped);
                BonesSSBO *ssboData = static_cast<BonesSSBO *>(mapped);

                ssboData->bonesCount = static_cast<int>(gpuEntity.finalBones.size());

                for (int i = 0; i < gpuEntity.finalBones.size(); ++i)
                {
                    ssboData->boneMatrices[i] = gpuEntity.finalBones.at(i);
                }

                m_bonesSSBOs[m_currentFrame]->unmap();
            }

            vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout->vk(), 0, static_cast<uint32_t>(descriptorSets.size()),
                                    descriptorSets.data(), 0, nullptr);

            vkCmdDrawIndexed(commandBuffer->vk(), mesh->indicesCount, 1, 0, 0, 0);
        }
    }

    for (auto &addData : data.additionalData)
    {
        for (const auto &wireframeEntity : addData.stencilMeshes)
        {
            for (const auto &m : wireframeEntity.meshes)
            {
                core::PipelineLayout::SharedPtr pipelineLayout = engineShaderFamilies::wireframeMeshShaderFamily.pipelineLayout;

                vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_stencilGraphicsPipeline->vk());

                VkBuffer vertexBuffers[] = {m->vertexBuffer->vk()};
                VkDeviceSize offset[] = {0};

                vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);
                vkCmdBindIndexBuffer(commandBuffer->vk(), m->indexBuffer->vk(), 0, m->indexType);

                ModelPushConstant modelPushConstant{
                    .model = wireframeEntity.transform};

                vkCmdPushConstants(commandBuffer->vk(), pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ModelPushConstant), &modelPushConstant);

                std::vector<VkDescriptorSet> descriptorSets;

                descriptorSets =
                    {
                        m_cameraWireframeDescriptorSets[m_currentFrame] // set 0: camera
                    };
                vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout->vk(), 0, static_cast<uint32_t>(descriptorSets.size()),
                                        descriptorSets.data(), 0, nullptr);

                vkCmdDrawIndexed(commandBuffer->vk(), m->indicesCount, 1, 0, 0, 0);
            }
        }
    }

    m_skybox->render(commandBuffer, data.view, data.projection);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END