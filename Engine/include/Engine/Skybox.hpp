#ifndef ELIX_SKYBOX_HPP
#define ELIX_SKYBOX_HPP

#include "Core/Macros.hpp"
#include "Core/RenderPass.hpp"
#include "Core/GraphicsPipeline.hpp"
#include "Core/Buffer.hpp"
#include "Core/CommandPool.hpp"
#include "Core/DescriptorSetLayout.hpp"
#include "Core/PipelineLayout.hpp"

#include "Engine/TextureImage.hpp"

#include <string>
#include <array>

#include <glm/glm.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Skybox
{
public:
    Skybox(VkDevice device, VkPhysicalDevice physicalDevice, core::CommandPool::SharedPtr commandPool, core::RenderPass::SharedPtr renderPass, const std::array<std::string, 6>& cubemaps,
    VkDescriptorPool descriptorPool);

    //TODO FIX IT
    void render(core::CommandBuffer::SharedPtr commandBuffer, const glm::mat4& view, const glm::mat4& projection);
private:
    core::Buffer::SharedPtr m_vertexBuffer{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};
    TextureImage::SharedPtr m_skyboxTexture{nullptr};
    core::GraphicsPipeline::SharedPtr m_graphicsPipeline{nullptr};
    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    VkDescriptorSet m_descriptorSet{VK_NULL_HANDLE};
    uint32_t m_vertexCount{0};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_SKYBOX_HPP