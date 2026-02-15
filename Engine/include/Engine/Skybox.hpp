#ifndef ELIX_SKYBOX_HPP
#define ELIX_SKYBOX_HPP

#include "Core/Macros.hpp"
#include "Core/GraphicsPipeline.hpp"
#include "Core/Buffer.hpp"
#include "Core/DescriptorSetLayout.hpp"
#include "Core/PipelineLayout.hpp"

#include "Engine/Texture.hpp"

#include <string>
#include <array>

#include <glm/glm.hpp>

#include "Engine/Builders/GraphicsPipelineKey.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Skybox
{
public:
    Skybox(const std::array<std::string, 6> &cubemaps, VkDescriptorPool descriptorPool);
    Skybox(const std::string &hdrPath, VkDescriptorPool descriptorPool);

    // TODO FIX IT
    void render(core::CommandBuffer::SharedPtr commandBuffer, const glm::mat4 &view, const glm::mat4 &projection, core::GraphicsPipeline::SharedPtr graphicsPipeline);

    const GraphicsPipelineKey &getGraphicsPipelineKey() const;

private:
    void createResources(VkDescriptorPool descriptorPool);

    GraphicsPipelineKey m_graphicsPipelineKey;

    core::Buffer::SharedPtr m_vertexBuffer{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};
    Texture::SharedPtr m_skyboxTexture{nullptr};
    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};
    uint32_t m_vertexCount{0};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SKYBOX_HPP