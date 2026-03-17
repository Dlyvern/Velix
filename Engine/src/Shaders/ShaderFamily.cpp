#include "Engine/Shaders/ShaderFamily.hpp"
#include "Core/VulkanContext.hpp"

#include "Engine/Shaders/PushConstant.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void EngineShaderFamilies::initEngineShaderFamilies()
{
    const auto &device = core::VulkanContext::getContext()->getDevice();

    {
        const bool hasAccelerationStructureSupport = core::VulkanContext::getContext() &&
                                                     core::VulkanContext::getContext()->hasAccelerationStructureSupport();

        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                      VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        uboLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding lightSpaceBinding{};
        lightSpaceBinding.binding = 1;
        lightSpaceBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        lightSpaceBinding.descriptorCount = 1;
        lightSpaceBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                       VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        lightSpaceBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding lightSSBOLayoutBinding{};
        lightSSBOLayoutBinding.binding = 2;
        lightSSBOLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lightSSBOLayoutBinding.descriptorCount = 1;
        lightSSBOLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT |
                                            VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                                            VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        lightSSBOLayoutBinding.pImmutableSamplers = nullptr;

        std::vector<VkDescriptorSetLayoutBinding> cameraBindings{
            uboLayoutBinding,
            lightSpaceBinding,
            lightSSBOLayoutBinding};

        if (hasAccelerationStructureSupport)
        {
            VkDescriptorSetLayoutBinding accelerationStructureBinding{};
            accelerationStructureBinding.binding = 3;
            accelerationStructureBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            accelerationStructureBinding.descriptorCount = 1;
            accelerationStructureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT |
                                                      VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                                                      VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            accelerationStructureBinding.pImmutableSamplers = nullptr;

            cameraBindings.push_back(accelerationStructureBinding);
        }

        // All bindings default to 0 (fully bound); TLAS (binding 3) is partially bound
        // so it can be legally unset when no TLAS is available (e.g. empty scene, first frame).
        std::vector<VkDescriptorBindingFlags> cameraBindingFlags(cameraBindings.size(), 0u);
        if (hasAccelerationStructureSupport)
            cameraBindingFlags.back() = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

        cameraDescriptorSetLayout = core::DescriptorSetLayout::createShared(device, cameraBindings, cameraBindingFlags);
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

        VkDescriptorSetLayoutBinding instanceBinding{};
        instanceBinding.binding = 1;
        instanceBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        instanceBinding.descriptorCount = 1;
        instanceBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        instanceBinding.pImmutableSamplers = nullptr;

        objectDescriptorSetLayout = core::DescriptorSetLayout::createShared(device,
                                                                            std::vector<VkDescriptorSetLayoutBinding>{bonesBinding, instanceBinding});
    }

    meshShaderFamily.layouts.push_back(cameraDescriptorSetLayout);
    meshShaderFamily.layouts.push_back(materialDescriptorSetLayout);
    meshShaderFamily.layouts.push_back(objectDescriptorSetLayout);

    struct PushConstants
    {
        uint32_t baseInstance{0};
        uint32_t padding[3]{0, 0, 0};
    };

    const std::vector<VkPushConstantRange> pushConstantsStatic{
        PushConstant<PushConstants>::getRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)};

    meshShaderFamily.pushConstantRange = PushConstant<PushConstants>::getRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    std::vector<std::reference_wrapper<const core::DescriptorSetLayout>> layoutRefs;
    layoutRefs.reserve(meshShaderFamily.layouts.size());

    for (const auto &layout : meshShaderFamily.layouts)
        layoutRefs.emplace_back(*layout);

    meshShaderFamily.pipelineLayout = core::PipelineLayout::createShared(device, layoutRefs, pushConstantsStatic);

    // ---- Bindless material descriptor set layout ----
    // binding 0: sampler2D allTextures[MAX_BINDLESS_TEXTURES] — UPDATE_AFTER_BIND + PARTIALLY_BOUND
    // binding 1: readonly SSBO MaterialParams[]              — PARTIALLY_BOUND
    {
        VkDescriptorSetLayoutBinding texBinding{};
        texBinding.binding         = 0;
        texBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texBinding.descriptorCount = EngineShaderFamilies::MAX_BINDLESS_TEXTURES;
        texBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding ssboBinding{};
        ssboBinding.binding         = 1;
        ssboBinding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ssboBinding.descriptorCount = 1;
        ssboBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorBindingFlags bindingFlags[2] = {
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
        };

        VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
        flagsInfo.bindingCount  = 2;
        flagsInfo.pBindingFlags = bindingFlags;

        VkDescriptorSetLayoutBinding bindings[2] = {texBinding, ssboBinding};

        VkDescriptorSetLayoutCreateInfo layoutCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutCI.pNext        = &flagsInfo;
        layoutCI.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutCI.bindingCount = 2;
        layoutCI.pBindings    = bindings;

        vkCreateDescriptorSetLayout(device, &layoutCI, nullptr,
                                    &EngineShaderFamilies::bindlessMaterialSetLayout);
    }

    // ---- Bindless mesh pipeline layout (Set 0=camera, Set 1=bindless, Set 2=object) ----
    {
        VkDescriptorSetLayout setLayouts[3] = {
            EngineShaderFamilies::cameraDescriptorSetLayout->vk(),
            EngineShaderFamilies::bindlessMaterialSetLayout,
            EngineShaderFamilies::objectDescriptorSetLayout->vk()
        };

        // Match the GBufferPC push-constant: 4 uints + 1 float = 20 bytes.
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset     = 0;
        pushRange.size       = 20;

        VkPipelineLayoutCreateInfo pipelineLayoutCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutCI.setLayoutCount         = 3;
        pipelineLayoutCI.pSetLayouts            = setLayouts;
        pipelineLayoutCI.pushConstantRangeCount = 1;
        pipelineLayoutCI.pPushConstantRanges    = &pushRange;

        vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr,
                               &EngineShaderFamilies::bindlessMeshPipelineLayout);
    }
}

void EngineShaderFamilies::cleanEngineShaderFamilies()
{
    const auto d = core::VulkanContext::getContext()->getDevice();

    if (bindlessMeshPipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(d, bindlessMeshPipelineLayout, nullptr);
        bindlessMeshPipelineLayout = VK_NULL_HANDLE;
    }
    if (bindlessMaterialSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(d, bindlessMaterialSetLayout, nullptr);
        bindlessMaterialSetLayout = VK_NULL_HANDLE;
    }

    meshShaderFamily.pipelineLayout->destroyVk();

    for (auto &descriptorSetLayout : meshShaderFamily.layouts)
        descriptorSetLayout->destroyVk();
}

ELIX_NESTED_NAMESPACE_END
