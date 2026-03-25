#include "Engine/Render/BindlessRegistry.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include "Core/Buffer.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void BindlessRegistry::initialize(VkDevice device)
{
    if (EngineShaderFamilies::bindlessMaterialSetLayout == VK_NULL_HANDLE)
        return; // ShaderFamily not initialised yet

    m_device = device;

    {
        VkDescriptorPoolSize sizes[2];
        sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sizes[0].descriptorCount = EngineShaderFamilies::MAX_BINDLESS_TEXTURES;
        sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sizes[1].descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolCI.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolCI.maxSets = 1;
        poolCI.poolSizeCount = 2;
        poolCI.pPoolSizes = sizes;
        vkCreateDescriptorPool(device, &poolCI, nullptr, &m_bindlessPool);
    }

    {
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = m_bindlessPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &EngineShaderFamilies::bindlessMaterialSetLayout;
        vkAllocateDescriptorSets(device, &allocInfo, &m_bindlessSet);
    }

    // Material params SSBO (written CPU-side each frame, read GPU-side in shaders).
    const VkDeviceSize ssboSize =
        static_cast<VkDeviceSize>(EngineShaderFamilies::MAX_BINDLESS_MATERIALS) * sizeof(Material::GPUParams);

    m_materialParamsSSBO = core::Buffer::createShared(
        ssboSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU);
    m_cpuMaterialParams.resize(EngineShaderFamilies::MAX_BINDLESS_MATERIALS);

    // Wire binding 1 to the SSBO.
    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = m_materialParamsSSBO->vk();
    bufInfo.offset = 0;
    bufInfo.range = ssboSize;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_bindlessSet;
    write.dstBinding = 1;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void BindlessRegistry::cleanup(VkDevice device)
{
    m_materialParamsSSBO.reset();
    m_cpuMaterialParams.clear();
    m_textureRegistry.clear();
    m_materialRegistry.clear();
    m_nextTextureSlot = 0;
    m_nextMaterialSlot = 0;

    if (m_bindlessPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, m_bindlessPool, nullptr);
        m_bindlessPool = VK_NULL_HANDLE;
        m_bindlessSet = VK_NULL_HANDLE;
    }
}

uint32_t BindlessRegistry::getOrRegisterTexture(Texture *tex)
{
    if (!tex)
        tex = Texture::getDefaultWhiteTexture().get();

    auto it = m_textureRegistry.find(tex);
    if (it != m_textureRegistry.end())
        return it->second;

    if (m_nextTextureSlot >= EngineShaderFamilies::MAX_BINDLESS_TEXTURES)
    {
        VX_ENGINE_WARNING_STREAM("BindlessRegistry: MAX_BINDLESS_TEXTURES reached, returning slot 0");
        return 0;
    }

    const uint32_t slot = m_nextTextureSlot++;
    m_textureRegistry[tex] = slot;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = tex->vkImageView();
    imageInfo.sampler = tex->vkSampler();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = m_bindlessSet;
    write.dstBinding = 0;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

    return slot;
}

uint32_t BindlessRegistry::getOrRegisterMaterial(Material *mat)
{
    auto it = m_materialRegistry.find(mat);
    if (it != m_materialRegistry.end())
        return it->second;

    if (m_nextMaterialSlot >= EngineShaderFamilies::MAX_BINDLESS_MATERIALS)
    {
        VX_ENGINE_WARNING_STREAM("BindlessRegistry: MAX_BINDLESS_MATERIALS reached, returning slot 0");
        return 0;
    }

    const uint32_t slot = m_nextMaterialSlot++;
    m_materialRegistry[mat] = slot;
    return slot;
}

void BindlessRegistry::uploadMaterialParams(uint32_t usedMaterialSlots)
{
    if (!m_materialParamsSSBO || usedMaterialSlots == 0)
        return;

    const VkDeviceSize uploadSize =
        static_cast<VkDeviceSize>(usedMaterialSlots) * sizeof(Material::GPUParams);
    m_materialParamsSSBO->upload(m_cpuMaterialParams.data(), uploadSize);
}

ELIX_NESTED_NAMESPACE_END