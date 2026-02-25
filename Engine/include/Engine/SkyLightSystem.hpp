#ifndef ELIX_SKY_LIGHT_SYSTEM_HPP
#define ELIX_SKY_LIGHT_SYSTEM_HPP

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"
#include "Core/DescriptorSetLayout.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/GraphicsPipeline.hpp"

#include "Engine/Texture.hpp"
#include "Engine/Builders/GraphicsPipelineKey.hpp"

#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class SkyLightSystem
{
public:
    SkyLightSystem();

    void render(core::CommandBuffer::SharedPtr commandBuffer, float strength, float deltaTime, const glm::mat4 &view, const glm::mat4 &projection, core::GraphicsPipeline::SharedPtr graphicsPipeline);

    void setTimeOfDay(float hour);
    void setSunDirection(const glm::vec3 &direction);

    const GraphicsPipelineKey &getGraphicsPipelineKey() const;

    glm::vec3 directionFromAzimuthElevation(float azimuthDeg, float elevationDeg);

private:
    void createResources();

    GraphicsPipelineKey m_graphicsPipelineKey;

    core::Buffer::SharedPtr m_vertexBuffer{nullptr};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};
    Texture::SharedPtr m_skyboxTexture{nullptr};
    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    uint32_t m_vertexCount{0};
    VkDescriptorSet m_descriptorSet;

    core::Buffer::SharedPtr m_uboBuffer{nullptr};
    void *m_mappedData{nullptr};

    glm::vec3 m_sunDirection{1.0};
    float m_time{0.0f};
    float m_cloudSpeed{0.5f};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SKY_LIGHT_SYSTEM_HPP