#include "Engine/Shaders/ShaderFamily.hpp"
#include "Core/VulkanContext.hpp"

#include "Engine/PushConstant.hpp"
#include <glm/glm.hpp>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void EngineShaderFamilies::initEngineShaderFamilies()
{
    //! look at me
    // staticMeshShaderFamily.layouts.reserve(3);
    const auto &device = core::VulkanContext::getContext()->getDevice();

    {
        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding lightSpaceBinding{};
        lightSpaceBinding.binding = 1;
        lightSpaceBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        lightSpaceBinding.descriptorCount = 1;
        lightSpaceBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        lightSpaceBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding lightMapBinding{};
        lightMapBinding.binding = 2;
        lightMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        lightMapBinding.descriptorCount = 1;
        lightMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        lightMapBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding lightSSBOLayoutBinding{};
        lightSSBOLayoutBinding.binding = 3;
        lightSSBOLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lightSSBOLayoutBinding.descriptorCount = 1;
        lightSSBOLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        lightSSBOLayoutBinding.pImmutableSamplers = nullptr;

        cameraDescriptorSetLayout = core::DescriptorSetLayout::createShared(device,
                                                                            std::vector<VkDescriptorSetLayoutBinding>{uboLayoutBinding, lightSpaceBinding, lightMapBinding, lightSSBOLayoutBinding});
    }

    {
        VkDescriptorSetLayoutBinding textureLayoutBinding{};
        textureLayoutBinding.binding = 0;
        textureLayoutBinding.descriptorCount = 1;
        textureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureLayoutBinding.pImmutableSamplers = nullptr;
        textureLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding colorLayoutBinding{};
        colorLayoutBinding.binding = 1;
        colorLayoutBinding.descriptorCount = 1;
        colorLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        colorLayoutBinding.pImmutableSamplers = nullptr;
        colorLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        materialDescriptorSetLayout = core::DescriptorSetLayout::createShared(device, std::vector<VkDescriptorSetLayoutBinding>{textureLayoutBinding, colorLayoutBinding});
    }

    {
        VkDescriptorSetLayoutBinding bonesBinding{};
        bonesBinding.binding = 0;
        bonesBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bonesBinding.descriptorCount = 1;
        bonesBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        bonesBinding.pImmutableSamplers = nullptr;

        objectDescriptorSetLayout = core::DescriptorSetLayout::createShared(device,
                                                                            std::vector<VkDescriptorSetLayoutBinding>{bonesBinding});
    }

    staticMeshShaderFamily.layouts.push_back(cameraDescriptorSetLayout);
    staticMeshShaderFamily.layouts.push_back(materialDescriptorSetLayout);

    skeletonMeshShaderFamily.layouts.push_back(cameraDescriptorSetLayout);
    skeletonMeshShaderFamily.layouts.push_back(materialDescriptorSetLayout);
    skeletonMeshShaderFamily.layouts.push_back(objectDescriptorSetLayout);

    wireframeMeshShaderFamily.layouts.push_back(cameraDescriptorSetLayout);

    texturedStatisMeshShaderFamily.layouts.push_back(cameraDescriptorSetLayout);
    texturedStatisMeshShaderFamily.layouts.push_back(materialDescriptorSetLayout);

    struct ModelPushConstant
    {
        glm::mat4 modelMatrix;
    };

    struct PushConstants
    {
        glm::mat4 modelMatrix; // offset 0, size 64 bytes
        uint32_t objectId;     // offset 64, size 4 bytes
        uint32_t padding[3];
    };

    const std::vector<VkPushConstantRange> pushConstants{
        PushConstant<ModelPushConstant>::getRange(VK_SHADER_STAGE_VERTEX_BIT)};

    const std::vector<VkPushConstantRange> pushConstantsStatic{
        PushConstant<PushConstants>::getRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)};

    skeletonMeshShaderFamily.pipelineLayout = core::PipelineLayout::createShared(device, skeletonMeshShaderFamily.layouts, pushConstantsStatic);
    staticMeshShaderFamily.pipelineLayout = core::PipelineLayout::createShared(device, staticMeshShaderFamily.layouts, pushConstantsStatic);
    wireframeMeshShaderFamily.pipelineLayout = core::PipelineLayout::createShared(device, wireframeMeshShaderFamily.layouts, pushConstants);
    texturedStatisMeshShaderFamily.pipelineLayout = core::PipelineLayout::createShared(device, texturedStatisMeshShaderFamily.layouts, pushConstants);
}

void EngineShaderFamilies::cleanEngineShaderFamilies()
{
    staticMeshShaderFamily.pipelineLayout->destroyVk();
    skeletonMeshShaderFamily.pipelineLayout->destroyVk();

    for (auto &descriptorSetLayout : staticMeshShaderFamily.layouts)
        descriptorSetLayout->destroyVk();

    for (auto &descriptorSetLayout : skeletonMeshShaderFamily.layouts)
        descriptorSetLayout->destroyVk();
}
ELIX_NESTED_NAMESPACE_END