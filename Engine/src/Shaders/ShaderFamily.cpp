#include "Engine/Shaders/ShaderFamily.hpp"
#include "Core/VulkanContext.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

core::DescriptorSetLayout::SharedPtr engineShaderFamilies::cameraDescriptorSetLayout = nullptr;
core::DescriptorSetLayout::SharedPtr engineShaderFamilies::staticMeshLightLayout = nullptr;
core::DescriptorSetLayout::SharedPtr engineShaderFamilies::staticMeshMaterialLayout = nullptr;
core::DescriptorSetLayout::SharedPtr engineShaderFamilies::skeletonMeshCameraLayout = nullptr;
core::DescriptorSetLayout::SharedPtr engineShaderFamilies::wireframeMeshCameraLayout = nullptr;

ShaderFamily engineShaderFamilies::staticMeshShaderFamily;

ShaderFamily engineShaderFamilies::skeletonMeshShaderFamily;
ShaderFamily engineShaderFamilies::wireframeMeshShaderFamily;

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

        wireframeMeshCameraLayout = wireframeMeshShaderFamily.layouts.emplace_back(core::DescriptorSetLayout::createShared(core::VulkanContext::getContext()->getDevice(),
                                                                                                                           std::vector<VkDescriptorSetLayoutBinding>{uboLayoutBinding}));
    }

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

        cameraDescriptorSetLayout = staticMeshShaderFamily.layouts.emplace_back(core::DescriptorSetLayout::createShared(core::VulkanContext::getContext()->getDevice(), std::vector<VkDescriptorSetLayoutBinding>{uboLayoutBinding, lightSpaceBinding, lightMapBinding}));
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

        VkDescriptorSetLayoutBinding bonesBinding{};
        bonesBinding.binding = 3;
        bonesBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bonesBinding.descriptorCount = 1;
        bonesBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        bonesBinding.pImmutableSamplers = nullptr;

        skeletonMeshCameraLayout = skeletonMeshShaderFamily.layouts.emplace_back(core::DescriptorSetLayout::createShared(core::VulkanContext::getContext()->getDevice(),
                                                                                                                         std::vector<VkDescriptorSetLayoutBinding>{
                                                                                                                             uboLayoutBinding,
                                                                                                                             lightSpaceBinding,
                                                                                                                             lightMapBinding,
                                                                                                                             bonesBinding}));
        skeletonMeshShaderFamily.layouts.push_back(staticMeshMaterialLayout);
        skeletonMeshShaderFamily.layouts.push_back(staticMeshLightLayout);
    }

    struct ModelPushConstant
    {
        glm::mat4 modelMatrix;
    };

    const std::vector<VkPushConstantRange> pushConstants{
        PushConstant<ModelPushConstant>::getRange(VK_SHADER_STAGE_VERTEX_BIT)};

    const std::vector<core::DescriptorSetLayout::SharedPtr> wireframeSetLayout{
        wireframeMeshCameraLayout};

    const std::vector<core::DescriptorSetLayout::SharedPtr> staticSetLayouts{
        cameraDescriptorSetLayout,
        staticMeshMaterialLayout,
        staticMeshLightLayout,
    };

    const std::vector<core::DescriptorSetLayout::SharedPtr> skeletonSetLayouts{
        skeletonMeshCameraLayout,
        staticMeshMaterialLayout,
        staticMeshLightLayout,
    };

    skeletonMeshShaderFamily.pipelineLayout = core::PipelineLayout::createShared(core::VulkanContext::getContext()->getDevice(), skeletonSetLayouts, pushConstants);
    staticMeshShaderFamily.pipelineLayout = core::PipelineLayout::createShared(core::VulkanContext::getContext()->getDevice(), staticSetLayouts, pushConstants);
    wireframeMeshShaderFamily.pipelineLayout = core::PipelineLayout::createShared(core::VulkanContext::getContext()->getDevice(), wireframeSetLayout, pushConstants);
}

void engineShaderFamilies::cleanEngineShaderFamilies()
{
    staticMeshShaderFamily.pipelineLayout->destroyVk();

    for (auto &descriptorSetLayout : staticMeshShaderFamily.layouts)
        descriptorSetLayout->destroyVk();
}
ELIX_NESTED_NAMESPACE_END