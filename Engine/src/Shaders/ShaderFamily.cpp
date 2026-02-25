#include "Engine/Shaders/ShaderFamily.hpp"
#include "Core/VulkanContext.hpp"

#include "Engine/Shaders/PushConstant.hpp"
#include <glm/glm.hpp>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void EngineShaderFamilies::initEngineShaderFamilies()
{
    const auto &device = core::VulkanContext::getContext()->getDevice();

    {
        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding lightSpaceBinding{};
        lightSpaceBinding.binding = 1;
        lightSpaceBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        lightSpaceBinding.descriptorCount = 1;
        lightSpaceBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        lightSpaceBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding lightSSBOLayoutBinding{};
        lightSSBOLayoutBinding.binding = 2;
        lightSSBOLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lightSSBOLayoutBinding.descriptorCount = 1;
        lightSSBOLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        lightSSBOLayoutBinding.pImmutableSamplers = nullptr;

        cameraDescriptorSetLayout = core::DescriptorSetLayout::createShared(device,
                                                                            std::vector<VkDescriptorSetLayoutBinding>{uboLayoutBinding, lightSpaceBinding, lightSSBOLayoutBinding});
    }

    {
        VkDescriptorSetLayoutBinding albedoLayoutBinding{};
        albedoLayoutBinding.binding = 0;
        albedoLayoutBinding.descriptorCount = 1;
        albedoLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        albedoLayoutBinding.pImmutableSamplers = nullptr;
        albedoLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding normalLayoutBinding{};
        normalLayoutBinding.binding = 1;
        normalLayoutBinding.descriptorCount = 1;
        normalLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        normalLayoutBinding.pImmutableSamplers = nullptr;
        normalLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding ormLayoutBinding{};
        ormLayoutBinding.binding = 2;
        ormLayoutBinding.descriptorCount = 1;
        ormLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ormLayoutBinding.pImmutableSamplers = nullptr;
        ormLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding emissiveLayoutBinding{};
        emissiveLayoutBinding.binding = 3;
        emissiveLayoutBinding.descriptorCount = 1;
        emissiveLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        emissiveLayoutBinding.pImmutableSamplers = nullptr;
        emissiveLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding materialParamsLayoutBinding{};
        materialParamsLayoutBinding.binding = 4;
        materialParamsLayoutBinding.descriptorCount = 1;
        materialParamsLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        materialParamsLayoutBinding.pImmutableSamplers = nullptr;
        materialParamsLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        materialDescriptorSetLayout = core::DescriptorSetLayout::createShared(device, std::vector<VkDescriptorSetLayoutBinding>{albedoLayoutBinding,
                                                                                                                                normalLayoutBinding, ormLayoutBinding, emissiveLayoutBinding, materialParamsLayoutBinding});
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

    meshShaderFamily.layouts.push_back(cameraDescriptorSetLayout);
    meshShaderFamily.layouts.push_back(materialDescriptorSetLayout);
    meshShaderFamily.layouts.push_back(objectDescriptorSetLayout);

    struct PushConstants
    {
        glm::mat4 modelMatrix; // offset 0, size 64 bytes
        uint32_t objectId;     // offset 64, size 4 bytes
        uint32_t bonesOffset;  // offset 68, size 4 bytes
        uint32_t padding[2];
    };

    const std::vector<VkPushConstantRange> pushConstantsStatic{
        PushConstant<PushConstants>::getRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)};

    meshShaderFamily.pushConstantRange = PushConstant<PushConstants>::getRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    meshShaderFamily.pipelineLayout = core::PipelineLayout::createShared(device, meshShaderFamily.layouts, pushConstantsStatic);
}

void EngineShaderFamilies::cleanEngineShaderFamilies()
{
    meshShaderFamily.pipelineLayout->destroyVk();

    for (auto &descriptorSetLayout : meshShaderFamily.layouts)
        descriptorSetLayout->destroyVk();
}
ELIX_NESTED_NAMESPACE_END
