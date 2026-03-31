#ifndef ELIX_BINDLESS_REGISTRY_HPP
#define ELIX_BINDLESS_REGISTRY_HPP

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"
#include "Engine/Material.hpp"
#include "Engine/Texture.hpp"

#include <unordered_map>
#include <vector>
#include <cstdint>

#include <vulkan/vulkan.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class BindlessRegistry
{
public:
    void initialize(VkDevice device, uint32_t framesInFlight);
    void cleanup(VkDevice device);

    /// Returns the bindless slot for a texture, registering it on first call.
    /// Falls back to the white-texture slot if tex is null.
    uint32_t getOrRegisterTexture(Texture *tex);

    /// Returns the material index for a material, registering it on first call.
    uint32_t getOrRegisterMaterial(Material *mat);

    /// Flushes accumulated material params to the GPU SSBO.
    void uploadMaterialParams(uint32_t frameIndex, uint32_t usedMaterialSlots);

    /// Synchronizes newly-registered textures into the descriptor set for a
    /// frame slot that has already been fenced by the caller.
    void syncFrame(uint32_t frameIndex);

    VkDescriptorSet getBindlessSet(uint32_t frameIndex) const
    {
        return frameIndex < m_bindlessSets.size() ? m_bindlessSets[frameIndex] : VK_NULL_HANDLE;
    }

    core::Buffer *getMaterialParamsSSBO(uint32_t frameIndex)
    {
        return frameIndex < m_materialParamsSSBOs.size() ? m_materialParamsSSBOs[frameIndex].get() : nullptr;
    }

    std::vector<Material::GPUParams> &getCpuMaterialParams() { return m_cpuMaterialParams; }

    uint32_t getRegisteredMaterialCount() const { return m_nextMaterialSlot; }
    uint32_t getRegisteredTextureCount() const { return m_nextTextureSlot; }

    bool isInitialized() const { return !m_bindlessSets.empty() && m_bindlessSets.front() != VK_NULL_HANDLE; }

private:
    VkDevice m_device{VK_NULL_HANDLE};
    VkDescriptorPool m_bindlessPool{VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> m_bindlessSets;

    std::vector<core::Buffer::SharedPtr> m_materialParamsSSBOs;
    std::vector<Material::GPUParams> m_cpuMaterialParams;
    std::vector<VkDescriptorImageInfo> m_registeredTextureInfos;
    std::vector<uint32_t> m_syncedTextureSlotsPerFrame;

    std::unordered_map<Texture *, uint32_t> m_textureRegistry;
    std::unordered_map<Material *, uint32_t> m_materialRegistry;

    uint32_t m_nextTextureSlot{0};
    uint32_t m_nextMaterialSlot{0};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_BINDLESS_REGISTRY_HPP
