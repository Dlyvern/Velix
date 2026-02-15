#include "Engine/Skybox.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Core/VulkanContext.hpp"

struct PushConstantView
{
    glm::mat4 view;
    glm::mat4 projection;
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Skybox::Skybox(const std::string &hdrPath, VkDescriptorPool descriptorPool)
{
    m_skyboxTexture = std::make_shared<Texture>();
    m_skyboxTexture->createCubemapFromHDR(hdrPath);

    createResources(descriptorPool);

    m_graphicsPipelineKey.shader = ShaderId::SkyboxHDR;
    m_graphicsPipelineKey.cull = CullMode::Front;
    m_graphicsPipelineKey.depthTest = true;
    m_graphicsPipelineKey.depthWrite = false;
    m_graphicsPipelineKey.depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
    m_graphicsPipelineKey.polygonMode = VK_POLYGON_MODE_FILL;
    m_graphicsPipelineKey.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    m_graphicsPipelineKey.pipelineLayout = m_pipelineLayout;
}

void Skybox::createResources(VkDescriptorPool descriptorPool)
{
    auto device = core::VulkanContext::getContext()->getDevice();

    const std::vector<float> skyboxVertices =
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
                          .build(device, descriptorPool, m_descriptorSetLayout);
}

const GraphicsPipelineKey &Skybox::getGraphicsPipelineKey() const
{
    return m_graphicsPipelineKey;
}

Skybox::Skybox(const std::array<std::string, 6> &cubemaps, VkDescriptorPool descriptorPool)
{
    m_skyboxTexture = std::make_shared<Texture>();
    m_skyboxTexture->loadCubemap(cubemaps);

    createResources(descriptorPool);

    m_graphicsPipelineKey.shader = ShaderId::Skybox;
    m_graphicsPipelineKey.cull = CullMode::Front;
    m_graphicsPipelineKey.depthTest = true;
    m_graphicsPipelineKey.depthWrite = false;
    m_graphicsPipelineKey.depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
    m_graphicsPipelineKey.polygonMode = VK_POLYGON_MODE_FILL;
    m_graphicsPipelineKey.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    m_graphicsPipelineKey.pipelineLayout = m_pipelineLayout;
}

void Skybox::render(core::CommandBuffer::SharedPtr commandBuffer, const glm::mat4 &view, const glm::mat4 &projection, core::GraphicsPipeline::SharedPtr graphicsPipeline)
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    glm::mat4 skyboxView = glm::mat4(glm::mat3(view));

    PushConstantView skyboxPushConstant{
        .view = skyboxView,
        .projection = projection};

    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offset[] = {0};

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offset);

    vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantView), &skyboxPushConstant);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    vkCmdDraw(commandBuffer, m_vertexCount, 1, 0, 0);
}

ELIX_NESTED_NAMESPACE_END