#include "Engine/Render/BindlessRegistry.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include "Core/Buffer.hpp"

#include <algorithm>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void BindlessRegistry::initialize(VkDevice device, uint32_t framesInFlight)
{
    if (EngineShaderFamilies::bindlessMaterialSetLayout == VK_NULL_HANDLE)
        return; // ShaderFamily not initialised yet

    m_device = device;
    const uint32_t setCount = std::max(1u, framesInFlight);

    {
        VkDescriptorPoolSize sizes[2];
        sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sizes[0].descriptorCount = EngineShaderFamilies::MAX_BINDLESS_TEXTURES * setCount;
        sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sizes[1].descriptorCount = setCount;

        VkDescriptorPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolCI.flags = 0;
        poolCI.maxSets = setCount;
        poolCI.poolSizeCount = 2;
        poolCI.pPoolSizes = sizes;
        vkCreateDescriptorPool(device, &poolCI, nullptr, &m_bindlessPool);
    }

    {
        m_bindlessSets.resize(setCount, VK_NULL_HANDLE);

        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = m_bindlessPool;
        allocInfo.descriptorSetCount = setCount;

        std::vector<VkDescriptorSetLayout> setLayouts(setCount, EngineShaderFamilies::bindlessMaterialSetLayout);
        allocInfo.pSetLayouts = setLayouts.data();
        vkAllocateDescriptorSets(device, &allocInfo, m_bindlessSets.data());
    }

    // Material params SSBO (written CPU-side each frame, read GPU-side in shaders).
    const VkDeviceSize ssboSize =
        static_cast<VkDeviceSize>(EngineShaderFamilies::MAX_BINDLESS_MATERIALS) * sizeof(Material::GPUParams);

    m_materialParamsSSBOs.resize(setCount);
    m_cpuMaterialParams.resize(EngineShaderFamilies::MAX_BINDLESS_MATERIALS);
    m_syncedTextureSlotsPerFrame.assign(setCount, 0u);

    for (uint32_t frameIndex = 0; frameIndex < setCount; ++frameIndex)
    {
        m_materialParamsSSBOs[frameIndex] = core::Buffer::createShared(
            ssboSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU);

        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = m_materialParamsSSBOs[frameIndex]->vk();
        bufInfo.offset = 0;
        bufInfo.range = ssboSize;

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = m_bindlessSets[frameIndex];
        write.dstBinding = 1;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bufInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
}

void BindlessRegistry::cleanup(VkDevice device)
{
    m_materialParamsSSBOs.clear();
    m_cpuMaterialParams.clear();
    m_textureRegistry.clear();
    m_materialRegistry.clear();
    m_registeredTextureInfos.clear();
    m_syncedTextureSlotsPerFrame.clear();
    m_nextTextureSlot = 0;
    m_nextMaterialSlot = 0;
    m_bindlessSets.clear();

    if (m_bindlessPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, m_bindlessPool, nullptr);
        m_bindlessPool = VK_NULL_HANDLE;
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
    m_registeredTextureInfos.push_back(imageInfo);

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

void BindlessRegistry::uploadMaterialParams(uint32_t frameIndex, uint32_t usedMaterialSlots)
{
    if (frameIndex >= m_materialParamsSSBOs.size() || !m_materialParamsSSBOs[frameIndex] || usedMaterialSlots == 0)
        return;

    const VkDeviceSize uploadSize =
        static_cast<VkDeviceSize>(usedMaterialSlots) * sizeof(Material::GPUParams);
    m_materialParamsSSBOs[frameIndex]->upload(m_cpuMaterialParams.data(), uploadSize);
}

void BindlessRegistry::syncFrame(uint32_t frameIndex)
{
    if (frameIndex >= m_bindlessSets.size() ||
        frameIndex >= m_syncedTextureSlotsPerFrame.size() ||
        m_bindlessSets[frameIndex] == VK_NULL_HANDLE)
    {
        return;
    }

    const uint32_t syncedTextureSlots = m_syncedTextureSlotsPerFrame[frameIndex];
    const uint32_t registeredTextureSlots = static_cast<uint32_t>(m_registeredTextureInfos.size());
    if (syncedTextureSlots >= registeredTextureSlots)
        return;

    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(registeredTextureSlots - syncedTextureSlots);

    for (uint32_t slot = syncedTextureSlots; slot < registeredTextureSlots; ++slot)
    {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = m_bindlessSets[frameIndex];
        write.dstBinding = 0;
        write.dstArrayElement = slot;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &m_registeredTextureInfos[slot];
        writes.push_back(write);
    }

    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    m_syncedTextureSlotsPerFrame[frameIndex] = registeredTextureSlots;
}

ELIX_NESTED_NAMESPACE_END
