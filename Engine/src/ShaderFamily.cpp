#include "Engine/ShaderFamily.hpp"
#include "Core/VulkanContext.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ShaderFamily engineShaderFamilies::staticMeshShaderFamily;
core::DescriptorSetLayout::SharedPtr engineShaderFamilies::staticMeshCameraLayout = nullptr;
core::DescriptorSetLayout::SharedPtr engineShaderFamilies::staticMeshLightLayout = nullptr;
core::DescriptorSetLayout::SharedPtr engineShaderFamilies::staticMeshMaterialLayout = nullptr;

void engineShaderFamilies::initEngineShaderFamilies()
{
    //! look at me
    // staticMeshShaderFamily.layouts.reserve(3);

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

        staticMeshCameraLayout = staticMeshShaderFamily.layouts.emplace_back(core::DescriptorSetLayout::createShared(core::VulkanContext::getContext()->getDevice(), std::vector<VkDescriptorSetLayoutBinding>{uboLayoutBinding, lightSpaceBinding, lightMapBinding}));
    }

    {
        VkDescriptorSetLayoutBinding lightSSBOLayoutBinding{};
        lightSSBOLayoutBinding.binding = 0;
        lightSSBOLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lightSSBOLayoutBinding.descriptorCount = 1;
        lightSSBOLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        lightSSBOLayoutBinding.pImmutableSamplers = nullptr;

        staticMeshLightLayout = staticMeshShaderFamily.layouts.emplace_back(core::DescriptorSetLayout::createShared(core::VulkanContext::getContext()->getDevice(), std::vector<VkDescriptorSetLayoutBinding>{lightSSBOLayoutBinding}));
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

        staticMeshMaterialLayout = staticMeshShaderFamily.layouts.emplace_back(core::DescriptorSetLayout::createShared(core::VulkanContext::getContext()->getDevice(), std::vector<VkDescriptorSetLayoutBinding>{textureLayoutBinding, colorLayoutBinding}));
    }

    struct ModelPushConstant
    {
        glm::mat4 modelMatrix;
    };

    const std::vector<VkPushConstantRange> pushConstants{
        PushConstant<ModelPushConstant>::getRange(VK_SHADER_STAGE_VERTEX_BIT)};

    const std::vector<core::DescriptorSetLayout::SharedPtr> setLayouts{
        staticMeshCameraLayout,
        staticMeshMaterialLayout,
        staticMeshLightLayout,
    };

    staticMeshShaderFamily.pipelineLayout = core::PipelineLayout::createShared(core::VulkanContext::getContext()->getDevice(), setLayouts, pushConstants);
}

void engineShaderFamilies::cleanEngineShaderFamilies()
{
    staticMeshShaderFamily.pipelineLayout->destroyVk();

    for (auto &descriptorSetLayout : staticMeshShaderFamily.layouts)
        descriptorSetLayout->destroyVk();
}
ELIX_NESTED_NAMESPACE_END