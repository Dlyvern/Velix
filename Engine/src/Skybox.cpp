#include "Engine/Skybox.hpp"
#include "Engine/Shaders/PushConstant.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Utilities/BufferUtilities.hpp"
#include "Engine/Utilities/AsyncGpuUpload.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Core/VulkanContext.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

struct PushConstantView
{
    glm::mat4 view;
    glm::mat4 projection;
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Skybox::Skybox(const std::string &hdrPath, VkDescriptorPool descriptorPool)
{
    m_skyboxTexture = std::make_shared<Texture>();

    bool cubemapCreated = false;
    const std::string extensionLower = [&]()
    {
        std::string extension = std::filesystem::path(hdrPath).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return extension;
    }();

    if (extensionLower == ".elixasset")
    {
        auto textureAsset = AssetsLoader::loadTexture(hdrPath);
        if (textureAsset.has_value())
        {
            const auto &asset = textureAsset.value();
            if (asset.width > 0u && asset.height > 0u)
            {
                std::vector<float> rgbData;
                rgbData.resize(static_cast<size_t>(asset.width) * static_cast<size_t>(asset.height) * 3u);

                if (asset.encoding == TextureAsset::PixelEncoding::RGBA32F)
                {
                    const size_t floatCount = asset.pixels.size() / sizeof(float);
                    if (floatCount >= static_cast<size_t>(asset.width) * static_cast<size_t>(asset.height) * 4u)
                    {
                        const auto *rgbaData = reinterpret_cast<const float *>(asset.pixels.data());
                        for (size_t pixelIndex = 0; pixelIndex < static_cast<size_t>(asset.width) * static_cast<size_t>(asset.height); ++pixelIndex)
                        {
                            rgbData[pixelIndex * 3u + 0u] = rgbaData[pixelIndex * 4u + 0u];
                            rgbData[pixelIndex * 3u + 1u] = rgbaData[pixelIndex * 4u + 1u];
                            rgbData[pixelIndex * 3u + 2u] = rgbaData[pixelIndex * 4u + 2u];
                        }

                        cubemapCreated = m_skyboxTexture->createCubemapFromEquirectangular(rgbData.data(),
                                                                                            static_cast<int>(asset.width),
                                                                                            static_cast<int>(asset.height));
                    }
                }
                else if (asset.encoding == TextureAsset::PixelEncoding::RGBA8)
                {
                    const size_t byteCount = static_cast<size_t>(asset.width) * static_cast<size_t>(asset.height) * 4u;
                    if (asset.pixels.size() >= byteCount)
                    {
                        for (size_t pixelIndex = 0; pixelIndex < static_cast<size_t>(asset.width) * static_cast<size_t>(asset.height); ++pixelIndex)
                        {
                            rgbData[pixelIndex * 3u + 0u] = static_cast<float>(asset.pixels[pixelIndex * 4u + 0u]) / 255.0f;
                            rgbData[pixelIndex * 3u + 1u] = static_cast<float>(asset.pixels[pixelIndex * 4u + 1u]) / 255.0f;
                            rgbData[pixelIndex * 3u + 2u] = static_cast<float>(asset.pixels[pixelIndex * 4u + 2u]) / 255.0f;
                        }

                        cubemapCreated = m_skyboxTexture->createCubemapFromEquirectangular(rgbData.data(),
                                                                                            static_cast<int>(asset.width),
                                                                                            static_cast<int>(asset.height));
                    }
                }
            }
        }
    }

    if (!cubemapCreated)
        cubemapCreated = m_skyboxTexture->createCubemapFromHDR(hdrPath);

    if (!cubemapCreated)
        VX_ENGINE_ERROR_STREAM("Failed to create skybox from path: " << hdrPath << '\n');

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

    auto commandBuffer = core::CommandBuffer::createShared(*core::VulkanContext::getContext()->getTransferCommandPool());
    commandBuffer->begin();

    auto vertexStaging = core::Buffer::createShared(skyboxSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, core::memory::MemoryUsage::CPU_TO_GPU);
    vertexStaging->upload(skyboxVertices.data(), skyboxSize);

    m_vertexBuffer = core::Buffer::createShared(skyboxSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, core::memory::MemoryUsage::CPU_TO_GPU);

    utilities::BufferUtilities::copyBuffer(*vertexStaging, *m_vertexBuffer, *commandBuffer, skyboxSize);
    commandBuffer->end();

    if (!utilities::AsyncGpuUpload::submit(commandBuffer, core::VulkanContext::getContext()->getTransferQueue(), {vertexStaging}))
        VX_ENGINE_ERROR_STREAM("Failed to submit skybox vertex upload\n");

    VkDescriptorSetLayoutBinding cubemapBinding{};
    cubemapBinding.binding = 0;
    cubemapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    cubemapBinding.descriptorCount = 1;
    cubemapBinding.pImmutableSamplers = nullptr;
    cubemapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    m_descriptorSetLayout = core::DescriptorSetLayout::createShared(device, std::vector<VkDescriptorSetLayoutBinding>{cubemapBinding});

    auto pushConstant = PushConstant<PushConstantView>::getRange(VK_SHADER_STAGE_VERTEX_BIT);

    m_pipelineLayout = core::PipelineLayout::createShared(device,
                                                          std::vector<std::reference_wrapper<const core::DescriptorSetLayout>>{*m_descriptorSetLayout},
                                                          std::vector<VkPushConstantRange>{pushConstant});

    m_descriptorSet = DescriptorSetBuilder::begin()
                          .addImage(m_skyboxTexture->vkImageView(), m_skyboxTexture->vkSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0)
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

    renderGraph::profiling::cmdDraw(commandBuffer, m_vertexCount, 1, 0, 0);
}

ELIX_NESTED_NAMESPACE_END
