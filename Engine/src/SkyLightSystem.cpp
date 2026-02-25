#include "Engine/SkyLightSystem.hpp"

#include "Core/VulkanContext.hpp"

#include "Engine/Utilities/BufferUtilities.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"

#include <glm/glm.hpp>
#include <cstring>
#include <glm/gtc/constants.hpp>
struct PushConstantView
{
    glm::mat4 view;
    glm::mat4 projection;
};

struct UBO
{
    glm::vec4 sunDirection_time;  // xyz dir, w time
    glm::vec4 sunColor_intensity; // rgb color, w intensity
    glm::vec4 skyParams;          // x cloudSpeed, y cloudCoverage, z cloudDensity, w exposure
    glm::vec4 lightParams;        // x dirLightStrength, y starIntensity, z starDensity, w reserved
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)

SkyLightSystem::SkyLightSystem()
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

    auto commandBuffer = core::CommandBuffer::create(core::VulkanContext::getContext()->getGraphicsCommandPool());
    commandBuffer.begin();

    auto vertexStaging = core::Buffer::create(skyboxSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, core::memory::MemoryUsage::CPU_TO_GPU);
    vertexStaging.upload(skyboxVertices.data(), skyboxSize);

    m_vertexBuffer = core::Buffer::createShared(skyboxSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, core::memory::MemoryUsage::CPU_TO_GPU);

    utilities::BufferUtilities::copyBuffer(vertexStaging, *m_vertexBuffer, commandBuffer, skyboxSize);
    commandBuffer.end();

    commandBuffer.submit(core::VulkanContext::getContext()->getGraphicsQueue());
    vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
    ///

    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.pImmutableSamplers = nullptr;
    uboBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_uboBuffer = core::Buffer::createShared(sizeof(UBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU);
    m_uboBuffer->map(m_mappedData);
    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(device, std::vector<VkDescriptorSetLayoutBinding>{uboBinding});

    auto pushConstant = PushConstant<PushConstantView>::getRange(VK_SHADER_STAGE_VERTEX_BIT);

    m_pipelineLayout = core::PipelineLayout::createShared(device, std::vector<core::DescriptorSetLayout::SharedPtr>{m_descriptorSetLayout}, std::vector<VkPushConstantRange>{pushConstant});

    m_descriptorSet = DescriptorSetBuilder::begin()
                          .addBuffer(m_uboBuffer, sizeof(UBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                          .build(device, core::VulkanContext::getContext()->getPersistentDescriptorPool(), m_descriptorSetLayout);

    m_graphicsPipelineKey.shader = ShaderId::SkyLight;
    m_graphicsPipelineKey.cull = CullMode::None;
    m_graphicsPipelineKey.depthTest = true;
    m_graphicsPipelineKey.depthWrite = false;
    m_graphicsPipelineKey.depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
    m_graphicsPipelineKey.polygonMode = VK_POLYGON_MODE_FILL;
    m_graphicsPipelineKey.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    m_graphicsPipelineKey.pipelineLayout = m_pipelineLayout;
}

void SkyLightSystem::setTimeOfDay(float hour)
{
    // Simple day arc: sunrise ~6, noon ~12, sunset ~18
    float t = hour / 24.0f;

    float elevation = sin((t - 0.25f) * glm::two_pi<float>()) * 80.0f;
    float azimuth = t * 360.0f + 90.0f;

    m_sunDirection = directionFromAzimuthElevation(azimuth, elevation);
}

void SkyLightSystem::setSunDirection(const glm::vec3 &direction)
{
    m_sunDirection = direction;
}

const GraphicsPipelineKey &SkyLightSystem::getGraphicsPipelineKey() const
{
    return m_graphicsPipelineKey;
}

glm::vec3 SkyLightSystem::directionFromAzimuthElevation(float azimuthDeg, float elevationDeg)
{
    float az = glm::radians(azimuthDeg);
    float el = glm::radians(elevationDeg);

    float x = cos(el) * cos(az);
    float y = sin(el);
    float z = cos(el) * sin(az);

    return glm::normalize(glm::vec3(x, y, z));
}

void SkyLightSystem::render(core::CommandBuffer::SharedPtr commandBuffer, float strength, float deltaTime, const glm::mat4 &view, const glm::mat4 &projection, core::GraphicsPipeline::SharedPtr graphicsPipeline)
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    glm::mat4 skyboxView = glm::mat4(glm::mat3(view));

    m_time += deltaTime * m_cloudSpeed;

    PushConstantView skyboxPushConstant{
        .view = skyboxView,
        .projection = projection};

    UBO *uboData = static_cast<UBO *>(m_mappedData);
    uboData->sunDirection_time = glm::vec4(m_sunDirection, m_time);
    uboData->sunColor_intensity = glm::vec4(1.0f, 0.95f, 0.85f, 6.0f); // HDR-ish
    uboData->skyParams = glm::vec4(
        m_cloudSpeed, // x
        0.55f,        // coverage
        0.75f,        // density
        1.0f          // exposure
    );

    uboData->lightParams = glm::vec4(
        strength, // x (0 = sun off)
        1.0f,     // y star intensity
        0.65f,    // z star density
        0.0f);

    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offset[] = {0};

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offset);

    vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantView), &skyboxPushConstant);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    renderGraph::profiling::cmdDraw(commandBuffer, m_vertexCount, 1, 0, 0);
}

void SkyLightSystem::createResources()
{
}

ELIX_NESTED_NAMESPACE_END
