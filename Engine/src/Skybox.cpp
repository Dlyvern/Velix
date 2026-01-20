#include "Engine/Skybox.hpp"
#include "Engine/PushConstant.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Core/Shader.hpp"

#include "Core/VulkanContext.hpp"

struct PushConstantView
{
    glm::mat4 view;
    glm::mat4 projection;
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Skybox::Skybox(VkDevice device, VkPhysicalDevice physicalDevice, core::CommandPool::SharedPtr commandPool,
               core::RenderPass::SharedPtr renderPass, const std::array<std::string, 6> &cubemaps, VkDescriptorPool descriptorPool)
{
    std::vector<float> skyboxVertices =
        {
            -1.0f, 1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,
            1.0f, 1.0f, -1.0f,
            -1.0f, 1.0f, -1.0f,

            -1.0f, -1.0f, 1.0f,
            -1.0f, -1.0f, -1.0f,
            -1.0f, 1.0f, -1.0f,
            -1.0f, 1.0f, -1.0f,
            -1.0f, 1.0f, 1.0f,
            -1.0f, -1.0f, 1.0f,

            1.0f, -1.0f, -1.0f,
            1.0f, -1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,

            -1.0f, -1.0f, 1.0f,
            -1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, -1.0f, 1.0f,
            -1.0f, -1.0f, 1.0f,

            -1.0f, 1.0f, -1.0f,
            1.0f, 1.0f, -1.0f,
            1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,
            -1.0f, 1.0f, 1.0f,
            -1.0f, 1.0f, -1.0f,

            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f, 1.0f,
            1.0f, -1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f, 1.0f,
            1.0f, -1.0f, 1.0f};

    VkDeviceSize skyboxSize = sizeof(skyboxVertices[0]) * skyboxVertices.size();
    m_vertexCount = static_cast<uint32_t>(skyboxVertices.size()) / 3;

    m_vertexBuffer = core::Buffer::createCopied(skyboxVertices.data(), skyboxSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                core::memory::MemoryUsage::CPU_TO_GPU);

    m_skyboxTexture = std::make_shared<Texture>();
    m_skyboxTexture->loadCubemap(cubemaps, commandPool);

    VkDescriptorSetLayoutBinding cubemapBinding{};
    cubemapBinding.binding = 1;
    cubemapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    cubemapBinding.descriptorCount = 1;
    cubemapBinding.pImmutableSamplers = nullptr;
    cubemapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(device, std::vector<VkDescriptorSetLayoutBinding>{cubemapBinding});

    auto pushConstant = PushConstant<PushConstantView>::getRange(VK_SHADER_STAGE_VERTEX_BIT);

    m_pipelineLayout = core::PipelineLayout::createShared(device, std::vector<core::DescriptorSetLayout::SharedPtr>{m_descriptorSetLayout}, std::vector<VkPushConstantRange>{pushConstant});

    m_descriptorSet = DescriptorSetBuilder::begin()
                          .addImage(m_skyboxTexture->vkImageView(), m_skyboxTexture->vkSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1)
                          .build(device, descriptorPool, m_descriptorSetLayout->vk());

    core::Shader shader("./resources/shaders/skybox.vert.spv", "./resources/shaders/skybox.frag.spv");

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = 3 * sizeof(float);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescription{};
    attributeDescription.binding = 0;
    attributeDescription.location = 0;
    attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription.offset = 0;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

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

    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
    dynamicState.pDynamicStates = dynamicStates.data();

    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkViewport viewport = core::VulkanContext::getContext()->getSwapchain()->getViewport();
    VkRect2D scissor = core::VulkanContext::getContext()->getSwapchain()->getScissor();
    shaderStages = shader.getShaderStages();
    vertexBindingDescriptions = {bindingDescription};
    vertexAttributeDescriptions = {attributeDescription};

    VkPipelineVertexInputStateCreateInfo vertexInputStateCI{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInputStateCI.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindingDescriptions.size());
    vertexInputStateCI.pVertexBindingDescriptions = vertexBindingDescriptions.data();
    vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescriptions.size());
    vertexInputStateCI.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

    viewportState.pViewports = &viewport;
    viewportState.pScissors = &scissor;

    m_graphicsPipeline = std::make_shared<core::GraphicsPipeline>(device, renderPass->vk(), shaderStages.data(), static_cast<uint32_t>(shaderStages.size()), m_pipelineLayout->vk(), dynamicState, colorBlending, multisampling,
                                                                  rasterizer, viewportState, inputAssembly, vertexInputStateCI, subpass, depthStencil);
}

void Skybox::render(core::CommandBuffer::SharedPtr commandBuffer, const glm::mat4 &view, const glm::mat4 &projection)
{
    vkCmdBindPipeline(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline->vk());

    glm::mat4 skyboxView = glm::mat4(glm::mat3(view));

    PushConstantView skyboxPushConstant{
        .view = skyboxView,
        .projection = projection};

    VkBuffer vertexBuffers[] = {m_vertexBuffer->vk()};
    VkDeviceSize offset[] = {0};

    vkCmdBindVertexBuffers(commandBuffer->vk(), 0, 1, vertexBuffers, offset);

    vkCmdPushConstants(commandBuffer->vk(), m_pipelineLayout->vk(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantView), &skyboxPushConstant);

    vkCmdBindDescriptorSets(commandBuffer->vk(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout->vk(), 0, 1, &m_descriptorSet, 0, nullptr);

    vkCmdDraw(commandBuffer->vk(), m_vertexCount, 1, 0, 0);
}

ELIX_NESTED_NAMESPACE_END